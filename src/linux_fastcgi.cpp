#include "lib/uce_lib.cpp"
#include <csetjmp>

ServerState server_state;

#include "fastcgi/src/fcgicc.cc"

FastCGIServer server;
pid_t http_worker_pid = 0;
bool worker_accepts_http = false;
static sigjmp_buf request_fault_jmp;
static volatile sig_atomic_t request_fault_active = 0;
static volatile sig_atomic_t request_fault_signal = 0;
static Request* request_fault_request = 0;
static String request_fault_trace = "";

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
	return(expand_path(scope, get_cwd()));
}

String ws_message()
{
	if(!context)
		return("");
	return(context->call["message"].to_string());
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
	return(server.websocket_connection_ids(normalize_ws_scope(scope)));
}

u64 ws_connection_count(String scope)
{
	return(ws_connections(scope).size());
}

bool ws_send(String message, bool binary, String scope)
{
	return(server.websocket_broadcast(normalize_ws_scope(scope), message, binary) > 0);
}

bool ws_send_to(String connection_id, String message, bool binary)
{
	return(server.websocket_send_to(connection_id, message, binary));
}

bool ws_close(String connection_id)
{
	if(connection_id == "")
		connection_id = ws_connection_id();
	if(connection_id == "")
		return(false);
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
	request.stats.time_start = microtime();
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
		unlink(f.tmp_name);
	}

	if(failure_title == "" && request.session_id.length() > 0)
		save_session_data(request.session_id, request.session);

	cleanup_mysql_connections();
	restore_active_request(previous_context);

    return request.flags.status;
}

int handle_websocket_message(FastCGIRequest& request, const String& message, u8 opcode)
{
	Request event_request;
	ByteStream ws_output;

	Request* previous_context = set_active_request(event_request);
	server_state.request_count += 1;
	event_request.server = &server_state;
	event_request.params = request.params;
	event_request.params["REQUEST_METHOD"] = "WEBSOCKET";
	event_request.get = parse_query(event_request.params["QUERY_STRING"]);
	event_request.resources = request.resources;
	if(event_request.resources.websocket_connection_state)
		event_request.connection.set_reference(event_request.resources.websocket_connection_state);
	event_request.stats.time_init = microtime();
	event_request.stats.time_start = event_request.stats.time_init;
	event_request.random_index = 0;
	event_request.random_seed = gen_noise64(*reinterpret_cast<u64*>(&event_request.stats.time_start));
	event_request.response_code = "WEBSOCKET";
	event_request.header["Content-Type"] = context->server->config["CONTENT_TYPE"];
	event_request.in = message;
	event_request.ob = &ws_output;

	if(event_request.params["HTTP_COOKIE"].length() > 0)
		event_request.cookies = parse_cookies(event_request.params["HTTP_COOKIE"]);

	event_request.var["ws"]["message"] = message;
	event_request.var["ws"]["connection_id"] = request.resources.websocket_connection_id;
	event_request.var["ws"]["scope"] = request.resources.websocket_scope;
	event_request.var["ws"]["connection_count"] = (f64)server.websocket_connection_ids(request.resources.websocket_scope).size();
	event_request.var["ws"]["opcode"] = (f64)opcode;
	event_request.var["ws"]["is_binary"].set_bool(request.resources.websocket_is_binary);
	event_request.var["ws"]["is_text"].set_bool(request.resources.websocket_is_text);
	event_request.var["ws"]["document_uri"] = first(
		request.params["DOCUMENT_URI"],
		request.params["REQUEST_URI"]
	);

	event_request.call["message"] = message;
	event_request.call["connection_id"] = request.resources.websocket_connection_id;
	event_request.call["scope"] = request.resources.websocket_scope;
	event_request.call["opcode"] = (f64)opcode;
	event_request.call["document_uri"] = event_request.var["ws"]["document_uri"].to_string();

	compiler_invoke_websocket(&event_request, request.params["SCRIPT_FILENAME"]);

	if(event_request.session_id.length() > 0)
		save_session_data(event_request.session_id, event_request.session);

	cleanup_mysql_connections();
	restore_active_request(previous_context);
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

void listen_for_connections()
{
	if(precompile_jobs.size() > 0)
	{
		context = new Request();
		context->server = &server_state;
		for(auto s : precompile_jobs)
		{
			printf("- worker [%i] precompile %s\n", getpid(), s.c_str());
			get_shared_unit(context, s, false);
		}
	}

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
	for(;;)
	{
		server.process();
	}
}

void init_base_process()
{
	printf("(P) Starting parent server PID:%i\n", getpid());

	server_state.config = make_server_settings();
	server_state.config["COMPILER_SYS_PATH"] = get_cwd();
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

StringList init_precompile(u32& precompile_jobs_per_worker)
{
	StringList precompile_jobs_pending;
	if(server_state.config["PRECOMPILE_FILES_IN"] != "" && int_val(server_state.config["WORKER_COUNT"]) >= 2)
	{
		if(server_state.config["PRECOMPILE_FILES_IN"][0] != '/')
			server_state.config["PRECOMPILE_FILES_IN"] = expand_path(server_state.config["PRECOMPILE_FILES_IN"]);
		precompile_jobs_pending = split(trim(shell_exec(
			"find " +
			shell_escape(server_state.config["PRECOMPILE_FILES_IN"]) +
			" -iname '*.uce' ")), "\n");
		precompile_jobs_per_worker = 1 + (precompile_jobs_pending.size() / (int_val(server_state.config["WORKER_COUNT"]) -1));
	}
	return(precompile_jobs_pending);
}

int main(int argc, char** argv)
{
	StringList precompile_jobs_pending;
	u32 precompile_jobs_per_worker = 1;

	init_base_process();
	precompile_jobs_pending = init_precompile(precompile_jobs_per_worker);

	s32 worker_spawn_count = 0;

	for(;;)
	{
		while(workers.size() < int_val(server_state.config["WORKER_COUNT"]))
		{
			worker_spawn_count++;
			precompile_jobs.clear();
			// spawn workers with precompile jobs if necessary but
			// leave the first worker alone so it can start responding
			// to requests right away
			if(precompile_jobs_pending.size() > 0 && worker_spawn_count > 1)
			{
				for(u32 i = 0; i < precompile_jobs_per_worker; i++)
				{
					if(precompile_jobs_pending.size() > 0)
					{
						precompile_jobs.push_back(precompile_jobs_pending.back());
						precompile_jobs_pending.pop_back();
					}
				}
			}
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
