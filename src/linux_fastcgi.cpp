#include "lib/uce_lib.cpp"

ServerState server_state;
MemoryArena* request_arena = new MemoryArena(server_state.config.MAX_MEMORY, "request");

#include "fastcgi/src/fcgicc.cc"

FastCGIServer server;

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

	context = &request;
	server_state.request_count += 1;
	request.server = &server_state;
	request.stats.time_start = microtime();
    request.header["Content-Type"] = context->server->config.CONTENT_TYPE;
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

	// printf("(i) request ready\n");
    render_file(request.params["SCRIPT_FILENAME"]);

	for( auto &f : request.uploaded_files)
	{
		unlink(f.tmp_name);
	}

	if(request.session_id.length() > 0)
		save_session_data(request.session_id, request.session);

	cleanup_mysql_connections();

    return 0;
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
	server.on_request = &handle_request;
	server.on_data = &handle_data;
	server.on_complete = &handle_complete;
	/*
	server.request_handler(&handle_request);
	server.data_handler(&handle_data);
	server.complete_handler(&handle_complete);
	*/
	for(;;)
	{
		//if(request_arena) request_arena->clear();
		//current_memory_arena = request_arena;
		server.process();
	}
}

int main(int argc, char** argv)
{
	StringList precompile_jobs_pending;
	u32 precompile_jobs_per_worker = 1;

	printf("(P) Starting parent server PID:%i\n", getpid());

	signal(SIGCHLD, on_child_exit);
	srand(time());

	//if(server_state.config.COMPILER_SYS_PATH == "")
		server_state.config.COMPILER_SYS_PATH = get_cwd();

	// printf("MySQL client version: %s\n", mysql_get_client_info());

	printf("Compiler base path: %s\n", server_state.config.COMPILER_SYS_PATH.c_str());

	server_state.config.COMPILE_SCRIPT =
		server_state.config.COMPILER_SYS_PATH + "/" + server_state.config.COMPILE_SCRIPT;
	if(server_state.config.LISTEN_PORT)
		server.listen(server_state.config.LISTEN_PORT);
	if(server_state.config.SOCKET_PATH != "")
		server.listen(server_state.config.SOCKET_PATH);
	chmod(server_state.config.SOCKET_PATH.c_str(), S_IRWXU | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	dirname(server_state.config.COMPILER_SYS_PATH);
	basename(server_state.config.COMPILER_SYS_PATH);

	mkdir(server_state.config.BIN_DIRECTORY);
	mkdir(server_state.config.TMP_UPLOAD_PATH);
	mkdir(server_state.config.SESSION_PATH);

	//server.process(100);
	//server.process();
	/*try
	{
		server.process_forever();
	}
	catch (const std::runtime_error& e)
	{
		std::cout << e.what();
	}*/
	if(server_state.config.PRECOMPILE_FILES_IN != "" && server_state.config.WORKER_COUNT >= 2)
	{
		if(server_state.config.PRECOMPILE_FILES_IN != "/")
			server_state.config.PRECOMPILE_FILES_IN = expand_path(server_state.config.PRECOMPILE_FILES_IN);
		precompile_jobs_pending = split(trim(shell_exec(
			"find " +
			shell_escape(server_state.config.PRECOMPILE_FILES_IN) +
			" -iname '*.uce' ")), "\n");
		precompile_jobs_per_worker = 1 + (precompile_jobs_pending.size() / (server_state.config.WORKER_COUNT -1));
		/*
		*/
	}

	s32 worker_spawn_count = 0;

	for(;;)
	{
		while(workers.size() < server_state.config.WORKER_COUNT)
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
			spawn_subprocess(listen_for_connections);
		}
		sleep(1);
	}

	return 0;
}
