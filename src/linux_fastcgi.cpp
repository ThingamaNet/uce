#include "lib/uce_lib.cpp"

ServerState server_state;

#include "fastcgi/src/fcgicc.cc"

FastCGIServer server;
pid_t http_worker_pid = 0;
bool worker_accepts_http = false;

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
	return(context->var["ws"]["message"].to_string());
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

StringList ws_connections(String scope)
{
	return(server.websocket_connection_ids(normalize_ws_scope(scope)));
}

u64 ws_connection_count(String scope)
{
	return(ws_connections(scope).size());
}

bool ws_send(String message, String scope)
{
	return(server.websocket_broadcast(normalize_ws_scope(scope), message) > 0);
}

u64 ws_broadcast(String message, String scope)
{
	return(server.websocket_broadcast(normalize_ws_scope(scope), message));
}

bool ws_send_to(String connection_id, String message)
{
	return(server.websocket_send_to(connection_id, message));
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

	DTree call_param;
	compiler_invoke(&request, request.params["SCRIPT_FILENAME"], call_param);

	for( auto &f : request.uploaded_files)
	{
		unlink(f.tmp_name);
	}

	if(request.session_id.length() > 0)
		save_session_data(request.session_id, request.session);

	cleanup_mysql_connections();
	restore_active_request(previous_context);

    return 0;
}

int handle_websocket_message(FastCGIRequest& request, const String& message)
{
	Request event_request;
	ByteStream ws_output;
	DTree call_param;

	Request* previous_context = set_active_request(event_request);
	server_state.request_count += 1;
	event_request.server = &server_state;
	event_request.params = request.params;
	event_request.params["REQUEST_METHOD"] = "WEBSOCKET";
	event_request.get = parse_query(event_request.params["QUERY_STRING"]);
	event_request.resources = request.resources;
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
	event_request.var["ws"]["document_uri"] = first(
		request.params["DOCUMENT_URI"],
		request.params["REQUEST_URI"]
	);

	call_param["message"] = message;
	call_param["connection_id"] = request.resources.websocket_connection_id;
	call_param["scope"] = request.resources.websocket_scope;
	call_param["document_uri"] = event_request.var["ws"]["document_uri"].to_string();

	compiler_invoke_websocket(&event_request, request.params["SCRIPT_FILENAME"], call_param);

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
