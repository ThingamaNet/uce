#include "lib/uce_lib.cpp"
#include <csetjmp>
#include <deque>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

ServerState server_state;

#include "fastcgi/src/fcgicc.cc"

FastCGIServer server;
pid_t http_worker_pid = 0;
pid_t proactive_compiler_pid = 0;
pid_t websocket_exec_pid = 0;
bool worker_accepts_http = false;
static sigjmp_buf request_fault_jmp;
static volatile sig_atomic_t request_fault_active = 0;
static volatile sig_atomic_t request_fault_signal = 0;
static Request* request_fault_request = 0;
static String request_fault_trace = "";
static int websocket_exec_fd = -1;
static String websocket_exec_read_buffer = "";
static std::deque<DTree> websocket_exec_pending_jobs;
static DTree websocket_exec_inflight_job;
static String websocket_exec_write_buffer = "";

void close_inherited_server_sockets();

Request* set_active_request(Request& request)
{
	Request* previous_context = context;
	context = &request;
	return(previous_context);
}

void restore_active_request(Request* previous_context)
{
	context = previous_context;
}

String request_status_line(Request& request, int status_code, String reason)
{
	String status = std::to_string(status_code) + " " + reason;
	if(request.params["GATEWAY_INTERFACE"] != "")
		return("Status: " + status);
	return("HTTP/1.1 " + status);
}

void clear_request_output(Request& request)
{
	for(auto* stream : request.ob_stack)
		delete stream;
	request.ob_stack.clear();
	request.ob_start();
}

void render_request_failure(Request& request, String title, String details, String trace, int status_code = 500)
{
	request.response_code = request_status_line(request, status_code, "Internal Server Error");
	request.header.clear();
	request.set_cookies.clear();
	request.header["Content-Type"] = "text/plain; charset=utf-8";
	request.err.clear();

	Request* previous_context = set_active_request(request);
	clear_request_output(request);

	print("UCE runtime error\n");
	print("Request: ", first(request.params["REQUEST_URI"], request.params["SCRIPT_FILENAME"]), "\n");
	print("Script: ", request.params["SCRIPT_FILENAME"], "\n");
	print("Error: ", title, "\n");
	if(details != "")
		print("Details: ", details, "\n");
	if(request_fault_signal != 0)
	{
		String sig_label = signal_name((int)request_fault_signal);
		print("Signal: ", (s64)request_fault_signal);
		if(sig_label != "")
			print(" (", sig_label, ")");
		print("\n");
	}
	if(trace != "")
		print("\nTrace:\n", trace);

	request.err += "UCE runtime error\n";
	request.err += "Request: " + first(request.params["REQUEST_URI"], request.params["SCRIPT_FILENAME"]) + "\n";
	request.err += "Script: " + request.params["SCRIPT_FILENAME"] + "\n";
	request.err += "Error: " + title + "\n";
	if(details != "")
		request.err += "Details: " + details + "\n";
	if(request_fault_signal != 0)
	{
		String sig_label = signal_name((int)request_fault_signal);
		request.err += "Signal: " + std::to_string((int)request_fault_signal);
		if(sig_label != "")
			request.err += " (" + sig_label + ")";
		request.err += "\n";
	}
	if(trace != "")
		request.err += "\nTrace:\n" + trace;

	request.flags.status = status_code;
	restore_active_request(previous_context);
}

void on_request_fault_signal(int sig)
{
	request_fault_signal = sig;
	request_fault_trace = capture_backtrace_string(32, 1);
	if(request_fault_active && request_fault_request)
		siglongjmp(request_fault_jmp, 1);
	on_segfault(sig);
}

void install_request_fault_handlers()
{
	signal(SIGSEGV, on_request_fault_signal);
	signal(SIGABRT, on_request_fault_signal);
	signal(SIGBUS, on_request_fault_signal);
	signal(SIGILL, on_request_fault_signal);
	signal(SIGFPE, on_request_fault_signal);
}

void restore_request_fault_handlers()
{
	signal(SIGSEGV, on_segfault);
	signal(SIGABRT, on_segfault);
	signal(SIGBUS, on_segfault);
	signal(SIGILL, on_segfault);
	signal(SIGFPE, on_segfault);
}

namespace {

const char* websocket_ipc_base64_alphabet =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String websocket_ipc_base64_encode(String raw)
{
	String result;
	size_t i = 0;
	while(i < raw.length())
	{
		unsigned char input[3] = {0, 0, 0};
		size_t chunk = 0;
		for(; chunk < 3 && i < raw.length(); ++chunk, ++i)
			input[chunk] = (unsigned char)raw[i];

		result += websocket_ipc_base64_alphabet[input[0] >> 2];
		result += websocket_ipc_base64_alphabet[((input[0] & 0x03) << 4) | (input[1] >> 4)];
		result += (chunk > 1 ? websocket_ipc_base64_alphabet[((input[1] & 0x0F) << 2) | (input[2] >> 6)] : '=');
		result += (chunk > 2 ? websocket_ipc_base64_alphabet[input[2] & 0x3F] : '=');
	}
	return(result);
}

int websocket_ipc_base64_value(char c)
{
	if(c >= 'A' && c <= 'Z')
		return(c - 'A');
	if(c >= 'a' && c <= 'z')
		return(c - 'a' + 26);
	if(c >= '0' && c <= '9')
		return(c - '0' + 52);
	if(c == '+')
		return(62);
	if(c == '/')
		return(63);
	return(-1);
}

String websocket_ipc_base64_decode(String raw, bool& ok)
{
	ok = false;
	String filtered = "";
	for(auto c : raw)
	{
		if(!isspace((unsigned char)c))
			filtered.append(1, c);
	}
	if(filtered == "")
	{
		ok = true;
		return("");
	}
	if(filtered.length() % 4 != 0)
		return("");

	String result;
	for(size_t i = 0; i < filtered.length(); i += 4)
	{
		int values[4] = {0, 0, 0, 0};
		int padding = 0;
		for(int j = 0; j < 4; ++j)
		{
			char c = filtered[i + j];
			if(c == '=')
			{
				values[j] = 0;
				padding += 1;
				continue;
			}
			values[j] = websocket_ipc_base64_value(c);
			if(values[j] < 0)
				return("");
		}

		result.append(1, (char)((values[0] << 2) | (values[1] >> 4)));
		if(padding < 2)
			result.append(1, (char)(((values[1] & 0x0F) << 4) | (values[2] >> 2)));
		if(padding < 1)
			result.append(1, (char)(((values[2] & 0x03) << 6) | values[3]));
	}

	ok = true;
	return(result);
}

bool websocket_ipc_set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if(flags == -1)
		return(false);
	return(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

bool websocket_exec_enabled_for_process()
{
	return(worker_accepts_http);
}

u64 websocket_exec_queue_limit_bytes()
{
	u64 configured = int_val(server_state.config["WEBSOCKET_EXEC_QUEUE_BYTES"]);
	if(configured < 64 * 1024)
		configured = 1024 * 1024;
	return(configured);
}

bool websocket_exec_has_inflight_job()
{
	return(websocket_exec_inflight_job.to_bool());
}

FastCGIServer::Connection* websocket_find_connection(String connection_id)
{
	for(auto& item : server.client_sockets)
	{
		FastCGIServer::Connection* connection = item.second;
		if(connection->is_websocket && connection->websocket_connection_id == connection_id)
			return(connection);
	}
	return(0);
}

void websocket_exec_clear_ipc_state()
{
	if(websocket_exec_fd != -1)
		close(websocket_exec_fd);
	websocket_exec_fd = -1;
	websocket_exec_read_buffer = "";
	websocket_exec_write_buffer = "";
	websocket_exec_inflight_job.clear();
}

void websocket_exec_close_connection(String connection_id, u16 status_code = 1011, String reason = "websocket handler unavailable")
{
	if(connection_id == "")
		return;
	server.websocket_close(connection_id, status_code, reason);
}

void websocket_exec_fail_inflight_job(String reason = "websocket handler unavailable")
{
	if(!websocket_exec_has_inflight_job())
		return;
	websocket_exec_close_connection(websocket_exec_inflight_job["connection_id"].to_string(), 1011, reason);
	websocket_exec_inflight_job.clear();
	websocket_exec_write_buffer = "";
}

void websocket_exec_queue_job(DTree job)
{
	if(job["connection_id"].to_string() == "")
		return;

	u64 queued_bytes = websocket_exec_write_buffer.length();
	if(websocket_exec_has_inflight_job())
		queued_bytes += websocket_exec_inflight_job["serialized"].to_string().length();
	for(auto& pending : websocket_exec_pending_jobs)
		queued_bytes += pending["serialized"].to_string().length();
	queued_bytes += json_encode(job).length();

	if(queued_bytes > websocket_exec_queue_limit_bytes())
	{
		printf("(!) websocket dispatch queue overflow for %s\n", job["connection_id"].to_string().c_str());
		websocket_exec_close_connection(job["connection_id"].to_string(), 1013, "websocket server busy");
		return;
	}

	job["serialized"] = json_encode(job) + "\n";
	websocket_exec_pending_jobs.push_back(job);
}

StringList websocket_exec_snapshot_connections(String scope)
{
	return(server.websocket_connection_ids(scope));
}

void websocket_exec_append_command(DTree command)
{
	if(!context)
		return;
	context->resources.websocket_dispatch_commands.push(command);
}

void websocket_exec_apply_command(DTree command)
{
	String action = command["action"].to_string();
	if(action == "broadcast")
	{
		bool ok = false;
		String payload = websocket_ipc_base64_decode(command["message_b64"].to_string(), ok);
		if(!ok)
			return;
		server.websocket_broadcast(command["scope"].to_string(), payload, command["binary"].to_bool());
		return;
	}
	if(action == "send_to")
	{
		bool ok = false;
		String payload = websocket_ipc_base64_decode(command["message_b64"].to_string(), ok);
		if(!ok)
			return;
		server.websocket_send_to(command["connection_id"].to_string(), payload, command["binary"].to_bool());
		return;
	}
	if(action == "close")
	{
		server.websocket_close(
			command["connection_id"].to_string(),
			(u16)command["status_code"].to_u64(),
			command["reason"].to_string()
		);
	}
}

void websocket_exec_apply_result(DTree result)
{
	if(result["type"].to_string() != "result")
		return;

	String inflight_connection_id = websocket_exec_inflight_job["connection_id"].to_string();
	String result_connection_id = result["connection_id"].to_string();
	if(inflight_connection_id != "" && result_connection_id != "" && inflight_connection_id != result_connection_id)
	{
		printf("(!) websocket dispatch result mismatch: expected %s got %s\n",
			inflight_connection_id.c_str(),
			result_connection_id.c_str());
	}

	FastCGIServer::Connection* connection = websocket_find_connection(result_connection_id);
	if(connection)
		connection->websocket_state = result["connection_state"];

	result["commands"].each([] (DTree command, String) {
		websocket_exec_apply_command(command);
	});

	websocket_exec_inflight_job.clear();
}

void websocket_exec_handle_ipc_line(String line)
{
	line = trim(line);
	if(line == "")
		return;
	DTree result = json_decode(line);
	websocket_exec_apply_result(result);
}

void websocket_exec_read_results()
{
	if(websocket_exec_fd == -1)
		return;

	char buffer[4096];
	for(;;)
	{
		ssize_t read_result = read(websocket_exec_fd, buffer, sizeof(buffer));
		if(read_result == 0)
		{
			printf("(!) websocket executor disconnected\n");
			websocket_exec_fail_inflight_job();
			websocket_exec_clear_ipc_state();
			return;
		}
		if(read_result < 0)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				break;
			perror("websocket executor read");
			websocket_exec_fail_inflight_job();
			websocket_exec_clear_ipc_state();
			return;
		}

		websocket_exec_read_buffer.append(buffer, read_result);
		for(;;)
		{
			size_t line_end = websocket_exec_read_buffer.find('\n');
			if(line_end == String::npos)
				break;
			String line = websocket_exec_read_buffer.substr(0, line_end);
			websocket_exec_read_buffer.erase(0, line_end + 1);
			websocket_exec_handle_ipc_line(line);
		}
	}
}

void websocket_exec_flush_queue()
{
	if(websocket_exec_fd == -1)
		return;

	if(websocket_exec_write_buffer == "" && !websocket_exec_has_inflight_job() && websocket_exec_pending_jobs.size() > 0)
	{
		websocket_exec_inflight_job = websocket_exec_pending_jobs.front();
		websocket_exec_pending_jobs.pop_front();
		websocket_exec_write_buffer = websocket_exec_inflight_job["serialized"].to_string();
	}

	while(websocket_exec_write_buffer != "")
	{
		ssize_t write_result = write(websocket_exec_fd, websocket_exec_write_buffer.data(), websocket_exec_write_buffer.length());
		if(write_result < 0)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				return;
			perror("websocket executor write");
			websocket_exec_fail_inflight_job();
			websocket_exec_clear_ipc_state();
			return;
		}
		if(write_result == 0)
			return;
		websocket_exec_write_buffer.erase(0, write_result);
	}
}

Request websocket_exec_build_event_request(DTree job, String message)
{
	Request event_request;
	event_request.server = &server_state;
	event_request.params = job["params"].to_stringmap();
	event_request.params["REQUEST_METHOD"] = "WEBSOCKET";
	event_request.get = parse_query(event_request.params["QUERY_STRING"]);
	event_request.resources.is_websocket = true;
	event_request.resources.websocket_connection_id = job["connection_id"].to_string();
	event_request.resources.websocket_scope = job["scope"].to_string();
	event_request.resources.websocket_opcode = (u8)job["opcode"].to_u64();
	event_request.resources.websocket_is_binary = job["is_binary"].to_bool();
	event_request.resources.websocket_is_text = job["is_text"].to_bool();
	event_request.resources.websocket_dispatch_capture = true;
	job["scope_connections"].each([&] (DTree item, String) {
		event_request.resources.websocket_scope_connection_ids.push_back(item.to_string());
	});
	event_request.connection = job["connection_state"];
	event_request.stats.time_init = time_precise();
	event_request.stats.time_start = event_request.stats.time_init;
	event_request.random_index = 0;
	event_request.random_seed = gen_noise64(*reinterpret_cast<u64*>(&event_request.stats.time_start));
	event_request.response_code = "WEBSOCKET";
	event_request.header["Content-Type"] = server_state.config["CONTENT_TYPE"];
	event_request.in = message;

	if(event_request.params["HTTP_COOKIE"].length() > 0)
		event_request.cookies = parse_cookies(event_request.params["HTTP_COOKIE"]);

	event_request.var["ws"]["message"] = message;
	event_request.var["ws"]["connection_id"] = event_request.resources.websocket_connection_id;
	event_request.var["ws"]["scope"] = event_request.resources.websocket_scope;
	event_request.var["ws"]["connection_count"] = (f64)event_request.resources.websocket_scope_connection_ids.size();
	event_request.var["ws"]["opcode"] = (f64)event_request.resources.websocket_opcode;
	event_request.var["ws"]["is_binary"].set_bool(event_request.resources.websocket_is_binary);
	event_request.var["ws"]["is_text"].set_bool(event_request.resources.websocket_is_text);
	event_request.var["ws"]["document_uri"] = first(
		event_request.params["DOCUMENT_URI"],
		event_request.params["REQUEST_URI"]
	);

	event_request.call["message"] = message;
	event_request.call["connection_id"] = event_request.resources.websocket_connection_id;
	event_request.call["scope"] = event_request.resources.websocket_scope;
	event_request.call["opcode"] = (f64)event_request.resources.websocket_opcode;
	event_request.call["document_uri"] = event_request.var["ws"]["document_uri"].to_string();
	return(event_request);
}

bool websocket_exec_send_response(int fd, DTree response)
{
	String encoded = json_encode(response) + "\n";
	size_t offset = 0;
	while(offset < encoded.length())
	{
		ssize_t write_result = write(fd, encoded.data() + offset, encoded.length() - offset);
		if(write_result < 0)
		{
			if(errno == EINTR)
				continue;
			return(false);
		}
		offset += (size_t)write_result;
	}
	return(true);
}

void websocket_exec_process_job_line(int fd, String line)
{
	line = trim(line);
	if(line == "")
		return;

	DTree job = json_decode(line);
	if(job["type"].to_string() != "dispatch")
		return;

	bool decoded = false;
	String message = websocket_ipc_base64_decode(job["message_b64"].to_string(), decoded);
	if(!decoded)
	{
		printf("(!) invalid websocket IPC payload for %s\n", job["connection_id"].to_string().c_str());
		return;
	}

	Request event_request = websocket_exec_build_event_request(job, message);
	Request* previous_context = set_active_request(event_request);
	server_state.request_count += 1;

	compiler_invoke_websocket(&event_request, event_request.params["SCRIPT_FILENAME"]);

	if(event_request.session_id.length() > 0)
		save_session_data(event_request.session_id, event_request.session);
	cleanup_mysql_connections();

	DTree response;
	response["type"] = "result";
	response["connection_id"] = event_request.resources.websocket_connection_id;
	response["connection_state"] = event_request.connection;
	response["commands"] = event_request.resources.websocket_dispatch_commands;

	restore_active_request(previous_context);
	if(!websocket_exec_send_response(fd, response))
		exit(1);
}

void websocket_exec_child_loop(int fd)
{
	Request background_context;
	my_pid = getpid();
	context = &background_context;
	close_inherited_server_sockets();
	signal(SIGSEGV, on_segfault);
	signal(SIGABRT, on_segfault);
	signal(SIGBUS, on_segfault);
	signal(SIGILL, on_segfault);
	signal(SIGFPE, on_segfault);
	signal(SIGPIPE, SIG_IGN);
	setpriority(PRIO_PROCESS, 0, 5);

	String read_buffer;
	char buffer[4096];
	for(;;)
	{
		ssize_t read_result = read(fd, buffer, sizeof(buffer));
		if(read_result == 0)
			exit(0);
		if(read_result < 0)
		{
			if(errno == EINTR)
				continue;
			exit(1);
		}

		read_buffer.append(buffer, read_result);
		for(;;)
		{
			size_t line_end = read_buffer.find('\n');
			if(line_end == String::npos)
				break;
			String line = read_buffer.substr(0, line_end);
			read_buffer.erase(0, line_end + 1);
			websocket_exec_process_job_line(fd, line);
		}
	}
}

bool websocket_exec_alive()
{
	return(websocket_exec_pid > 0 && task_kill(websocket_exec_pid, 0) == 0 && websocket_exec_fd != -1);
}

void ensure_websocket_executor()
{
	if(!websocket_exec_enabled_for_process())
		return;
	if(websocket_exec_alive())
		return;

	if(websocket_exec_pid > 0 || websocket_exec_fd != -1)
	{
		websocket_exec_fail_inflight_job();
		websocket_exec_clear_ipc_state();
		websocket_exec_pid = 0;
	}

	int sockets[2] = {-1, -1};
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
	{
		perror("socketpair");
		return;
	}

	pid_t p = fork();
	if(p < 0)
	{
		perror("fork");
		close(sockets[0]);
		close(sockets[1]);
		return;
	}
	if(p == 0)
	{
		parent_pid = getppid();
		file_release_process_locks("websocket executor fork");
		prctl(PR_SET_PDEATHSIG, SIGHUP);
		close(sockets[0]);
		websocket_exec_child_loop(sockets[1]);
		exit(0);
	}

	close(sockets[1]);
	if(!websocket_ipc_set_nonblocking(sockets[0]))
	{
		printf("(!) failed to set websocket executor socket nonblocking\n");
		close(sockets[0]);
		return;
	}

	websocket_exec_fd = sockets[0];
	websocket_exec_pid = p;
	printf("(P) websocket executor spawned: PID %i\n", p);
}

void websocket_exec_tick()
{
	if(!websocket_exec_enabled_for_process())
		return;
	if(!websocket_exec_alive())
	{
		websocket_exec_fail_inflight_job();
		websocket_exec_clear_ipc_state();
		websocket_exec_pid = 0;
		ensure_websocket_executor();
	}
	websocket_exec_read_results();
	websocket_exec_flush_queue();
}

}

String current_ws_scope()
{
	if(!context)
		return("");
	return(first(
		context->resources.websocket_scope,
		context->params["SCRIPT_FILENAME"]
	));
}

String normalize_ws_scope(String scope)
{
	if(scope == "")
		return(current_ws_scope());
	if(scope[0] == '/')
		return(scope);
	return(expand_path(scope, cwd_get()));
}

String ws_message()
{
	if(!context)
		return("");
	return(context->call["message"].to_string());
}

bool config_truthy(String raw, bool default_value = true)
{
	raw = to_lower(trim(raw));
	if(raw == "")
		return(default_value);
	if(raw == "1" || raw == "true" || raw == "yes" || raw == "on")
		return(true);
	if(raw == "0" || raw == "false" || raw == "no" || raw == "off")
		return(false);
	return(default_value);
}

String ws_connection_id()
{
	if(!context)
		return("");
	return(context->resources.websocket_connection_id);
}

String ws_scope()
{
	return(current_ws_scope());
}

u8 ws_opcode()
{
	if(!context)
		return(0);
	return(context->resources.websocket_opcode);
}

bool ws_is_binary()
{
	if(!context)
		return(false);
	return(context->resources.websocket_is_binary);
}

StringList ws_connections(String scope)
{
	if(context && context->resources.websocket_dispatch_capture)
	{
		String normalized_scope = normalize_ws_scope(scope);
		if(normalized_scope == context->resources.websocket_scope)
			return(context->resources.websocket_scope_connection_ids);
		return(StringList());
	}
	return(server.websocket_connection_ids(normalize_ws_scope(scope)));
}

u64 ws_connection_count(String scope)
{
	return(ws_connections(scope).size());
}

bool ws_send(String message, bool binary, String scope)
{
	if(context && context->resources.websocket_dispatch_capture)
	{
		DTree command;
		command["action"] = "broadcast";
		command["scope"] = normalize_ws_scope(scope);
		command["binary"].set_bool(binary);
		command["message_b64"] = websocket_ipc_base64_encode(message);
		websocket_exec_append_command(command);
		return(true);
	}
	return(server.websocket_broadcast(normalize_ws_scope(scope), message, binary) > 0);
}

bool ws_send_to(String connection_id, String message, bool binary)
{
	if(context && context->resources.websocket_dispatch_capture)
	{
		DTree command;
		command["action"] = "send_to";
		command["connection_id"] = connection_id;
		command["binary"].set_bool(binary);
		command["message_b64"] = websocket_ipc_base64_encode(message);
		websocket_exec_append_command(command);
		return(true);
	}
	return(server.websocket_send_to(connection_id, message, binary));
}

bool ws_close(String connection_id)
{
	if(connection_id == "")
		connection_id = ws_connection_id();
	if(connection_id == "")
		return(false);
	if(context && context->resources.websocket_dispatch_capture)
	{
		DTree command;
		command["action"] = "close";
		command["connection_id"] = connection_id;
		command["status_code"] = (f64)1000;
		command["reason"] = "";
		websocket_exec_append_command(command);
		return(true);
	}
	return(server.websocket_close(connection_id));
}

int handle_request(FastCGIRequest& request) {
    // This is always the first event to occur.  It occurs when the
    // server receives all parameters.  There may be more data coming on the
    // standard input stream.
	if (request.params.count("REQUEST_URI"))
        return 0;  // OK, continue processing
    else
        return 1;  // stop processing and return error code
}

int handle_data(FastCGIRequest& request) {
    // This event occurs when data is received on the standard input stream.
    // A simple String is used to hold the input stream, so it is the
    // responsibility of the application to remember which data it has
    // processed. The application may modify it; new data will be appended
    // to it by the server. The same goes for the output and error streams:
    // the application should append data to them; the server will remove
    // all sent data from them.
    return 0;  // still OK

    std::transform(request.in.begin(), request.in.end(),
        std::back_inserter(request.err),
        std::bind1st(std::plus<char>(), 1));
    request.in.clear();  // don't process it again
    return 0;  // still OK
}

int handle_complete(FastCGIRequest& request) {
    // The event handler can also be a class member function. This
    // event occurs when the parameters and standard input streams are
    // both closed, and thus the request is complete.
	// printf("(i) request handle\n");

	Request* previous_context = set_active_request(request);
	server_state.request_count += 1;
	request.server = &server_state;
	request.stats.time_start = time_precise();
	//request.stats.mem_alloc = 0;
	//request.stats.mem_high = 0;
    request.header["Content-Type"] = context->server->config["CONTENT_TYPE"];
    request.get = parse_query(request.params["QUERY_STRING"]);
	request.random_index = 0;
	request.random_seed = gen_noise64(*reinterpret_cast<u64*>(&request.stats.time_start));
	request.ob_start();
	request_fault_request = &request;
	request_fault_active = 1;
	request_fault_signal = 0;
	request_fault_trace = "";
	install_request_fault_handlers();

	String failure_title = "";
	String failure_details = "";
	String failure_trace = "";

	if(sigsetjmp(request_fault_jmp, 1) != 0)
	{
		failure_title = "fatal signal during request";
		failure_details = "worker recovered before closing the upstream connection";
		failure_trace = request_fault_trace;
	}
	else
	{
		try
		{
			if(request.params["HTTP_COOKIE"].length() > 0)
				request.cookies = parse_cookies(request.params["HTTP_COOKIE"]);

			String ct_info = request.params["CONTENT_TYPE"];
			String ct_type = nibble(";", ct_info);

			if(request.params["REQUEST_METHOD"] == "POST")
			{
				if(ct_type == "multipart/form-data")
				{
					nibble("boundary=", ct_info);
					request.post = parse_multipart(request.in, String("--")+ct_info, request.uploaded_files);
				}
				else
				{
					request.post = parse_query(request.in);
				}
			}

			request.call = DTree();
			compiler_invoke(&request, request.params["SCRIPT_FILENAME"]);
		}
		catch(const std::exception& e)
		{
			failure_title = "uncaught exception during request";
			failure_details = e.what();
			failure_trace = capture_backtrace_string(32, 1);
		}
		catch(...)
		{
			failure_title = "unknown uncaught exception during request";
			failure_trace = capture_backtrace_string(32, 1);
		}
	}

	request_fault_active = 0;
	request_fault_request = 0;
	restore_request_fault_handlers();

	if(failure_title != "")
		render_request_failure(request, failure_title, failure_details, failure_trace, 500);

	for( auto &f : request.uploaded_files)
	{
		file_unlink(f.tmp_name);
	}

	if(failure_title == "" && request.session_id.length() > 0)
		save_session_data(request.session_id, request.session);

	cleanup_mysql_connections();
	restore_active_request(previous_context);

    return request.flags.status;
}

int handle_websocket_message(FastCGIRequest& request, const String& message, u8 opcode)
{
	ensure_websocket_executor();
	if(!websocket_exec_alive())
	{
		printf("(!) websocket executor unavailable for %s\n", request.resources.websocket_connection_id.c_str());
		server.websocket_close(request.resources.websocket_connection_id, 1011, "websocket handler unavailable");
		return(0);
	}

	DTree job;
	job["type"] = "dispatch";
	job["connection_id"] = request.resources.websocket_connection_id;
	job["scope"] = request.resources.websocket_scope;
	job["opcode"] = (f64)opcode;
	job["is_binary"].set_bool(request.resources.websocket_is_binary);
	job["is_text"].set_bool(request.resources.websocket_is_text);
	job["message_b64"] = websocket_ipc_base64_encode(message);
	if(request.resources.websocket_connection_state)
		job["connection_state"] = *request.resources.websocket_connection_state;

	for(auto& item : request.params)
		job["params"][item.first] = item.second;

	for(auto& connection_id : websocket_exec_snapshot_connections(request.resources.websocket_scope))
	{
		DTree snapshot_item;
		snapshot_item = connection_id;
		job["scope_connections"].push(snapshot_item);
	}

	websocket_exec_queue_job(job);
	return 0;
}

volatile bool termination_signal_received = false;

void on_terminate(int sig)
{
	if(termination_signal_received)
		return;
	termination_signal_received = true;
	if(getpid() != parent_pid)
		exit(1);
	printf("Terminating... PID %i:%i\n", getpid(), parent_pid);
	server.shutdown();
	exit(1);
}

void clear_shared_unit_cache(ServerState& state)
{
	for(auto& it : state.units)
		delete it.second;
	state.units.clear();
}

void close_inherited_server_sockets()
{
	for(auto socket_handle : server.server_sockets)
		close(socket_handle);
	server.server_sockets.clear();
	server.server_socket_types.clear();
}

bool proactive_compile_queue_has(StringList& queue, String file_name)
{
	return(std::find(queue.begin(), queue.end(), file_name) != queue.end());
}

void proactive_compile_queue_push(StringList& queue, String file_name)
{
	if(file_name == "" || proactive_compile_queue_has(queue, file_name))
		return;
	queue.push_back(file_name);
}

void run_proactive_compiler()
{
	Request background_context;
	StringList compile_queue;
	background_context.server = &server_state;
	if(!config_truthy(server_state.config["PROACTIVE_COMPILE_ENABLED"], true))
		return;
	f64 check_interval = float_val(server_state.config["PROACTIVE_COMPILE_CHECK_INTERVAL"]);
	f64 failure_retry_interval = 0;
	f64 next_scan_at = 0;
	std::map<String, f64> retry_after;
	if(check_interval < 1)
		check_interval = 1;
	failure_retry_interval = std::max(
		check_interval,
		(f64)std::max((s64)10, (s64)int_val(server_state.config["COMPILE_FAILURE_RETRY_SECONDS"]))
	);

	my_pid = getpid();
	context = &background_context;

	close_inherited_server_sockets();
	signal(SIGSEGV, on_segfault);
	signal(SIGABRT, on_segfault);
	signal(SIGBUS, on_segfault);
	signal(SIGILL, on_segfault);
	signal(SIGFPE, on_segfault);
	signal(SIGPIPE, SIG_IGN);
	setpriority(PRIO_PROCESS, 0, 10);

	auto known_units = compiler_list_known_units(&background_context);
	auto site_units = compiler_scan_site_units(&background_context);
	known_units.insert(known_units.end(), site_units.begin(), site_units.end());
	compiler_set_known_units(&background_context, known_units);
	next_scan_at = time_precise();

	for(;;)
	{
		if(compile_queue.size() == 0 && time_precise() >= next_scan_at)
		{
			auto tracked_units = compiler_list_known_units(&background_context);
			StringList existing_units;

			for(auto& file_name : tracked_units)
			{
				bool source_missing = false;
				auto retry_it = retry_after.find(file_name);
				bool retry_allowed = (retry_it == retry_after.end() || time_precise() >= retry_it->second);
				if(compiler_unit_needs_recompile(&background_context, file_name, &source_missing) && retry_allowed)
					proactive_compile_queue_push(compile_queue, file_name);
				if(source_missing)
				{
					printf("(i) proactive compiler forget removed unit %s\n", file_name.c_str());
					retry_after.erase(file_name);
					continue;
				}
				existing_units.push_back(file_name);
			}

			if(existing_units.size() != tracked_units.size())
				compiler_set_known_units(&background_context, existing_units);

			next_scan_at = time_precise() + check_interval;
		}

		if(compile_queue.size() > 0)
		{
			auto file_name = compile_queue.front();
			compile_queue.erase(compile_queue.begin());
			bool source_missing = false;
			auto retry_it = retry_after.find(file_name);
			if(retry_it != retry_after.end() && time_precise() < retry_it->second)
				continue;
			if(compiler_unit_needs_recompile(&background_context, file_name, &source_missing))
			{
				printf("(i) proactive compile %s\n", file_name.c_str());
				auto su = get_shared_unit(&background_context, file_name, false);
				if(su && su->compiler_messages == "")
					retry_after.erase(file_name);
				else
					retry_after[file_name] = time_precise() + failure_retry_interval;
			}
			else if(source_missing)
			{
				printf("(i) proactive compiler forget removed unit %s\n", file_name.c_str());
				compiler_untrack_known_unit(&background_context, file_name);
				retry_after.erase(file_name);
			}
			else
			{
				retry_after.erase(file_name);
			}
			background_context.session.clear();
			background_context.session_loaded_hash = "";
			clear_shared_unit_cache(server_state);
			usleep(250000);
			continue;
		}

		usleep(250000);
	}
}

bool proactive_compiler_alive()
{
	return(proactive_compiler_pid > 0 && task_kill(proactive_compiler_pid, 0) == 0);
}

void ensure_proactive_compiler()
{
	if(!config_truthy(server_state.config["PROACTIVE_COMPILE_ENABLED"], true))
		return;
	if(float_val(server_state.config["PROACTIVE_COMPILE_CHECK_INTERVAL"]) <= 0)
		return;

	if(proactive_compiler_alive())
		return;

	pid_t p = fork();
	if(p == 0)
	{
		file_release_process_locks("proactive compiler fork");
		prctl(PR_SET_PDEATHSIG, SIGHUP);
		run_proactive_compiler();
		exit(0);
	}

	proactive_compiler_pid = p;
	printf("(P) proactive compiler spawned: PID %i\n", p);
}

void listen_for_connections()
{
	signal(SIGSEGV, on_segfault);
	signal(SIGABRT, on_segfault);
	signal(SIGBUS, on_segfault);
	signal(SIGILL, on_segfault);
	signal(SIGFPE, on_segfault);
	signal(SIGPIPE, SIG_IGN);
	if(worker_accepts_http)
	{
		// Keep the dedicated HTTP/WebSocket worker alive. If it ages out like a
		// normal FastCGI worker, nginx can connect to the shared listening socket
		// while no child is actively accepting, which makes `.ws.uce` page loads
		// appear to hang until the parent respawns a replacement worker.
		server.calls_until_termination = -1;
	}
	if(!worker_accepts_http)
		server.close_http_listeners();
	server.on_request = &handle_request;
	server.on_data = &handle_data;
	server.on_complete = &handle_complete;
	server.on_websocket_message = &handle_websocket_message;
	if(worker_accepts_http)
		ensure_websocket_executor();
	for(;;)
	{
		file_release_process_locks("worker loop cleanup");
		server.process(worker_accepts_http ? 50 : -1);
		if(worker_accepts_http)
			websocket_exec_tick();
	}
}

void init_base_process()
{
	printf("(P) Starting parent server PID:%i\n", getpid());

	server_state.config = make_server_settings();
	server_state.config["COMPILER_SYS_PATH"] = cwd_get();
	printf("Compiler base path: %s\n", server_state.config["COMPILER_SYS_PATH"].c_str());

	server_state.config["COMPILE_SCRIPT"] =
		server_state.config["COMPILER_SYS_PATH"] + "/" + server_state.config["COMPILE_SCRIPT"];

	if(server_state.config["FCGI_PORT"] != "")
		server.listen(int_val(server_state.config["FCGI_PORT"]));

	printf("%s\n", var_dump(server_state.config).c_str());

	if(server_state.config["FCGI_SOCKET_PATH"] != "")
	{
		server.listen(server_state.config["FCGI_SOCKET_PATH"]);
		chmod(server_state.config["FCGI_SOCKET_PATH"].c_str(), S_IRWXU | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	}

	if(server_state.config["HTTP_PORT"] != "")
		server.listen_http(int_val(server_state.config["HTTP_PORT"]));

	mkdir(server_state.config["BIN_DIRECTORY"]);
	mkdir(server_state.config["TMP_UPLOAD_PATH"]);
	mkdir(server_state.config["SESSION_PATH"]);

	signal(SIGCHLD, on_child_exit);
	signal(SIGINT, on_terminate);
	signal(SIGPIPE, SIG_IGN);
	srand(time());
}

int main(int argc, char** argv)
{
	init_base_process();
	ensure_proactive_compiler();

	for(;;)
	{
		if(!proactive_compiler_alive())
			proactive_compiler_pid = 0;
		if(!termination_signal_received)
			ensure_proactive_compiler();

		while(workers.size() < int_val(server_state.config["WORKER_COUNT"]))
		{
			if(!termination_signal_received)
			{
				worker_accepts_http = (http_worker_pid == 0 || workers.count(http_worker_pid) == 0);
				pid_t child_pid = spawn_subprocess(listen_for_connections);
				if(child_pid > 0 && worker_accepts_http)
					http_worker_pid = child_pid;
			}
		}
		sleep(1);
	}

	return 0;
}
