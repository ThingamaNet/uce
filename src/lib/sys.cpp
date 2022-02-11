#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/file.h>
#include "sys.h"

String shell_exec(String cmd)
{
	//printf("(i) shell_exec(%s)\n", cmd.c_str());
	String data;
	FILE * stream;
	const int max_buffer = 256;
	char buffer[max_buffer];
	cmd.append(" 2>&1");

	stream = popen(cmd.c_str(), "r");

	if (stream) {
		while (!feof(stream))
			if (fgets(buffer, max_buffer, stream) != NULL) data.append(buffer);
		pclose(stream);
	}
	return data;
}

String shell_escape(String raw)
{
	// FIXME
	String result = "";
	for(auto c : raw)
	{
		if(c == '\'')
		{
			result.append("'\\''");
		}
		else
		{
			result.append(1, c);
		}
	}
	return("\'" + result + "\'");
	/*
	`	U+0060 (Grave Accent)	Backtick	Command substitution
~	U+007E	Tilde	Tilde expansion
!	U+0021	Exclamation mark	History expansion
#	U+0023 Number sign	Hash	Comments
$	U+0024	Dollar sign	Parameter expansion
&	U+0026	Ampersand	Background commands
*	U+002A	Asterisk	Filename expansion and globbing
(	U+0028	Left Parenthesis	Subshells
)	U+0029	Right Parenthesis	Subshells
   	U+0009	Tab (⇥)	Word splitting (whitespace)
{	U+007B Left Curly Bracket	Left brace	Brace expansion
[	U+005B	Left Square Bracket	Filename expansion and globbing
|	U+007C Vertical Line	Vertical bar	Pipelines
\	U+005C Reverse Solidus	Backslash	Escape character
;	U+003B	Semicolon	Separating commands
'	U+0027 Apostrophe	Single quote	String quoting
"	U+0022 Quotation Mark	Double quote	String quoting with interpolation
↩	U+000A Line Feed	Newline	Line break
<	U+003C	Less than	Input redirection
>	U+003E	Greater than	Output redirection
?	U+003F	Question mark	Filename expansion and globbing
  	U+0020	Space	Word splitting1 (whitespace)
 */
}

String basename(String fn)
{
	String result;
	while(fn.length() > 0)
		result = nibble("/", fn);
	//printf("basename(%s) %s\n", fn.c_str(), result.c_str());
	return(result);
}

String dirname(String fn)
{
	String result;
	auto seg = split(fn, "/");
	seg.pop_back();
	result = join(seg, "/");
	//printf("dirname(%s) %s seg#%i\n", fn.c_str(), result.c_str(), seg.size());
	return(result);
}

bool mkdir(String path)
{
	shell_exec(String("mkdir -p ")+" "+shell_escape(path));
	return(true);
}

bool file_exists(String path)
{
	std::filesystem::path fp{ path };
	return(std::filesystem::exists(fp));
}

String file_get_contents(String file_name)
{
	/*std::ifstream ifs(file_name);
	printf("stream file desc %i\n", ifs.filedesc());
	String content(
		(std::istreambuf_iterator<char>(ifs) ),
		(std::istreambuf_iterator<char>()    ) );*/
	char buf[512];
	String content;
	s32 fd = open(file_name.c_str(), O_RDONLY);
	if(fd == -1)
	{
		printf("(!) Could not read %s\n", file_name.c_str());
		return("");
	}
	flock(fd, LOCK_SH);
	s64 bytes_read = 0;
	//s64 size = lseek(fd, 0, SEEK_END);
	//lseek(fd, 0, SEEK_SET);
	//content.reserve(size+1);

	while((bytes_read = read(fd, buf, 512)) > 0)
	{
		content.append(buf, bytes_read);
	}

	flock(fd, LOCK_UN);
	close(fd);
	return(content);
}

bool file_put_contents(String file_name, String content)
{
	s32 fd = open(file_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
	if(fd == -1)
	{
		printf("(!) Could not write %s\n", file_name.c_str());
		return(false);
	}
	flock(fd, LOCK_EX);
	write(fd, content.data(), content.length());
	flock(fd, LOCK_UN);
	close(fd);
	return(true);
}

String get_cwd()
{
	return(std::filesystem::current_path());
}

void set_cwd(String path)
{
	chdir(path.c_str());
}

time_t file_mtime(String file_name)
{
	struct stat info;
	if (stat(file_name.c_str(), &info) != 0)
	{
		return(0);
	}
	else
	{
		return(info.st_mtime);
	}
}

void unlink(String file_name)
{
	remove(file_name.c_str());
}

String expand_path(String path, String relative_to_path)
{
	String result;

	if(relative_to_path == "")
		relative_to_path = get_cwd();

	auto base_path = split(relative_to_path, "/");
	auto rel_path = split(path, "/");

	for(auto& s : rel_path)
	{
		if(s == "..")
		{
			base_path.pop_back();
		}
		else if(s == ".")
		{

		}
		else
		{
			base_path.push_back(s);
		}
	}

	return(join(base_path, "/"));
}

f64 microtime()
{
	return ((f64)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count()) / 1000000;
}

u64 time()
{
	return(std::time(0));
}

String date(String format, u64 timestamp)
{
	String ts;
	String fmt;
	if(timestamp > 0)
		ts = String("-d '@")+std::to_string(timestamp)+"'";
	if(format != "")
		fmt = String("+'"+format+"'");
	return(trim(shell_exec("date "+ts+" "+fmt)));
}

String gmdate(String format, u64 timestamp)
{
	String ts;
	String fmt;
	if(timestamp > 0)
		ts = String("-d '@")+std::to_string(timestamp)+"'";
	if(format == "RFC1123")
		format = "%a, %d %b %Y %T GMT";
	if(format != "")
		fmt = String("+'"+format+"'");
	return(trim(shell_exec("date -u "+ts+" "+fmt)));
}

u64 parse_time(String time_String)
{
	return(int_val(trim(shell_exec("date -u -d '"+time_String+"' +'%s'"))));
}

u64 socket_connect(String host, short port)
{

	/*String addrinfo {
		int              ai_flags;
		int              ai_family;
		int              ai_socktype;
		int              ai_protocol;
		socklen_t        ai_addrlen;
		String sockaddr *ai_addr;
		char            *ai_canonname;
		String addrinfo *ai_next;
	};*/

    auto sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sockfd < 0)
    {
		print("SOCKET ERROR (could not open socket)\n");
		perror("SOCKET ERROR ");
		return(0);
	}

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(host.c_str());

    if(connect(sockfd, (struct sockaddr*) &addr, sizeof(addr)) < 0)
    {
		print("SOCKET ERROR (could not connect to address " + String(host) + ":" + std::to_string(port) + ")\n");
		perror("SOCKET ERROR ");
		return(0);
	}
	context->resources.sockets.push_back(sockfd);
	return(sockfd);
}

void socket_close(u64 sockfd)
{
	close(sockfd);
}

bool socket_write(u64 sockfd, String data)
{
	return(write(sockfd, data.c_str(), data.length()) >= 0);
}

String socket_read(u64 sockfd, u32 max_length, u32 timeout)
{
	struct timeval tv;
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
	char buf[max_length+1];
	auto byte_count = recv(sockfd, buf, sizeof(buf), 0);
	if(byte_count > 0)
	{
		buf[byte_count] = 0;
		String result(buf, byte_count+1);
		return(result);
	}
	return("");
}

String memcache_escape_key(String key)
{
	String result;
	for(auto c : key)
	{
		if(isspace(c))
			c = '_';
		result.append(1, c);
	}
	return(result);
}

StringList memcache_escape_keys(StringList keys)
{
	StringList result;
	for(auto s : keys)
	{
		result.push_back(memcache_escape_key(s));
	}
	return(result);
}

u64 memcache_connect(String host, short port)
{
	return(socket_connect(host, port));
}

String memcache_command(u64 connection, String command)
{
	socket_write(connection, command+"\r\n");
	return(socket_read(connection)); // FIXME: do multi-chunk until END line is received!
}

bool memcache_set(u64 connection, String key, String value, u64 expires_in)
{
	socket_write(connection,
		// set KEY META_DATA EXPIRY_TIME LENGTH_IN_BYTES
		String("set ") + memcache_escape_key(key) + " 0 " + std::to_string(expires_in) + " " + std::to_string(value.length()) + "\r\n" +
		value + "\r\n");
	return("STORED" == trim(socket_read(connection)));
}

bool memcache_delete(u64 connection, String key)
{
	socket_write(connection,
		// set KEY META_DATA EXPIRY_TIME LENGTH_IN_BYTES
		String("delete ") + memcache_escape_key(key) + "\r\n"
		);
	return("DELETED" == trim(socket_read(connection)));
}

String memcache_get(u64 connection, String key, String default_value)
{
	auto res = memcache_command(connection, String("get ")+memcache_escape_key(key));
	String t = nibble(res, " ");
	if(t == "VALUE")
	{
		String key = nibble(res, " ");
		String meta = nibble(res, " ");
		u32 length = stoi(nibble(res, "\r\n"));
		return(res.substr(0, length));
	}
	return(default_value);
}

StringMap memcache_get_multiple(u64 connection, StringList keys)
{
	StringMap result;
	// to do: escape key String
	auto res = memcache_command(connection, String("get ")+join(memcache_escape_keys(keys), " "));
	while(res.length() > 0)
	{
		String t = nibble(res, " ");
		if(t == "VALUE")
		{
			String key = nibble(res, " ");
			String meta = nibble(res, " ");
			u32 length = stoi(nibble(res, "\r\n"));
			result[key] = res.substr(0, length);
			res = res.substr(length+2);
		}
	}
	return(result);
}

void on_segfault(int sig)
{
	void *array[10];
	size_t size;

	// get void*'s for all entries on the stack
	size = backtrace(array, 10);

	// print out all the frames to stderr
	fprintf(stderr, "SEG FAULT: %d:\n", sig);
	backtrace_symbols_fd(array, size, STDERR_FILENO);
	exit(1);
}

struct Worker {
	pid_t pid;
};

std::map<pid_t, Worker> workers;
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/prctl.h>

void spawn_subprocess(std::function<void()> exec_after_spawn)
{
	parent_pid = getpid();
	pid_t p;
	p = fork();
	if(p == 0)
	{
		my_pid = getpid();
		//printf("(C) child procress started, PID:%i\n", my_pid);
		prctl(PR_SET_PDEATHSIG, SIGHUP);
		exec_after_spawn();
	}
	else
	{
		Worker w;
		w.pid = p;
		workers[w.pid] = w;
		printf("(P) child procress spawned: PID %i\n", p);
	}
}

pid_t task_pid(String key)
{
	String status_file_name = context->server->config["BIN_DIRECTORY"] + "/task-" + key;
	String status_file = file_get_contents(status_file_name);
	pid_t p = 0;
	if(status_file != "")
	{
		p = int_val(status_file);
		if(kill(p, 0) == 0) // process is still running
			return(p);
		unlink(status_file_name);
	}
	return(p);
}

pid_t task(String key, std::function<void()> exec_after_spawn, u64 timeout)
{
	String status_file_name = context->server->config["BIN_DIRECTORY"] + "/task-" + key;
	String status_file = file_get_contents(status_file_name);
	pid_t p;
	if(status_file != "")
	{
		p = int_val(status_file);
		if(kill(p, 0) == 0) // process is still running
		{
			printf("(P) worker process '%s' already running: PID %i\n", key.c_str(), p);
			return(p);
		}
		//printf("(P) worker process '%s' had crashed: PID %i\n", key.c_str(), p);
		unlink(status_file_name);
	}
	p = fork();
	if(p == 0)
	{
		my_pid = getpid();
		file_put_contents(status_file_name, std::to_string(my_pid));

		close(context->resources.fcgi_socket);
		context->resources.fcgi_socket = 0;
		//printf("(C) child procress started, PID:%i\n", my_pid);
		//prctl(PR_SET_PDEATHSIG, SIGHUP);
		exec_after_spawn();
		unlink(status_file_name);
		printf("(P) worker process '%s' terminated: PID %i\n", key.c_str(), my_pid);
		exit(0);
	}
	else
	{
		printf("(P) worker process '%s' spawned: PID %i\n", key.c_str(), p);
		return(p);
	}
}

void on_child_exit(int sig)
{
    pid_t pid;
    int   status;
    if ((pid = waitpid(-1, &status, WNOHANG)) != -1)
    {
		if(workers.count(pid) > 0)
		{
			workers.erase(pid);
			printf("(P) child terminated (PID:%i)\n", pid);
			//spawn_subprocess();
		}
    }
}

StringList ls(String dir)
{
	return(split(trim(shell_exec("ls -1 "+shell_escape(dir))), "\n"));
}

StringMap make_server_settings()
{
	StringMap cfg;

	cfg["BIN_DIRECTORY"] = "/tmp/uce/work";
	cfg["COMPILE_SCRIPT"] = "scripts/compile";
	cfg["SETUP_TEMPLATE"] = "scripts/setup.h.template";
	cfg["LIT_ESC"] = "3d5b5_1";
	cfg["CONTENT_TYPE"] = "text/html; charset=utf-8";
	cfg["SOCKET_PATH"] = "/run/uce.sock";
	cfg["TMP_UPLOAD_PATH"] = "/tmp/uce/uploads";
	cfg["SESSION_PATH"] = "/tmp/uce/sessions";
	cfg["COMPILER_SYS_PATH"] = ".";
	cfg["PRECOMPILE_FILES_IN"] = ".";

	cfg["LISTEN_PORT"] = std::to_string(9993);
	cfg["SESSION_TIME"] = std::to_string(60*60*24*30);
	cfg["WORKER_COUNT"] = std::to_string(4);
	cfg["MAX_MEMORY"] = std::to_string(1024*1024*16);

	for(auto& it : split_kv(file_get_contents("/etc/uce/settings.cfg")))
	{
		cfg[it.first] = it.second;
	}

	return(cfg);
}
