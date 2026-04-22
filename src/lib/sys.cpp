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

String capture_backtrace_string(u32 max_frames, u32 skip_frames)
{
	if(max_frames == 0)
		return("");

	std::vector<void*> frames(max_frames);
	size_t size = backtrace(frames.data(), max_frames);
	if(size == 0)
		return("");

	char** symbols = backtrace_symbols(frames.data(), size);
	if(!symbols)
		return("");

	String trace;
	for(size_t i = skip_frames; i < size; i++)
	{
		trace += symbols[i];
		trace += "\n";
	}
	free(symbols);
 	return(trace);
}

String signal_name(int sig)
{
	switch(sig)
	{
		case SIGABRT: return("SIGABRT");
		case SIGBUS: return("SIGBUS");
		case SIGFPE: return("SIGFPE");
		case SIGILL: return("SIGILL");
		case SIGINT: return("SIGINT");
		case SIGSEGV: return("SIGSEGV");
		case SIGTERM: return("SIGTERM");
		default: return("");
	}
}

namespace {

String time_format_expand_delta_tokens(String format, u64 timestamp, u64 now_timestamp)
{
	u64 delta_seconds = 0;
	if(now_timestamp > timestamp)
		delta_seconds = now_timestamp - timestamp;

	format = replace(format, "%deltaY", std::to_string(delta_seconds / (60 * 60 * 24 * 365)));
	format = replace(format, "%deltam", std::to_string(delta_seconds / (60 * 60 * 24 * 30)));
	format = replace(format, "%deltad", std::to_string(delta_seconds / (60 * 60 * 24)));
	format = replace(format, "%deltaH", std::to_string(delta_seconds / (60 * 60)));
	format = replace(format, "%deltaM", std::to_string(delta_seconds / 60));
	format = replace(format, "%deltaS", std::to_string(delta_seconds));
	return(format);
}

String time_format_shell(String format, u64 timestamp, bool use_utc)
{
	String ts;
	String fmt;
	u64 effective_timestamp = (timestamp > 0 ? timestamp : time());
	String expanded_format = time_format_expand_delta_tokens(format, effective_timestamp, time());

	if(timestamp > 0)
		ts = String("-d '@")+std::to_string(timestamp)+"'";
	if(expanded_format != "")
		fmt = String("+'")+expanded_format+"'";

	return(trim(shell_exec(String("date ") + (use_utc ? "-u " : "") + ts + " " + fmt)));
}

}

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

String path_join(String base, String child)
{
	if(base == "")
		return(child);
	if(child == "")
		return(base);
	if(child[0] == '/')
		return(child);
	if(base[base.length() - 1] == '/')
		return(base + child);
	return(base + "/" + child);
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

int file_open_locked(String file_name, int open_flags, int lock_type, int create_mode)
{
	int fd = open(file_name.c_str(), open_flags, create_mode);
	if(fd == -1)
		return(-1);
	if(flock(fd, lock_type) == -1)
	{
		close(fd);
		return(-1);
	}
	return(fd);
}

void file_close_locked(int fd)
{
	if(fd == -1)
		return;
	flock(fd, LOCK_UN);
	close(fd);
}

String file_get_contents_locked_fd(int fd)
{
	if(fd == -1)
		return("");

	char buf[512];
	String content;
	s64 bytes_read = 0;
	lseek(fd, 0, SEEK_SET);
	while((bytes_read = read(fd, buf, sizeof(buf))) > 0)
		content.append(buf, bytes_read);
	return(content);
}

namespace {

bool file_write_all(int fd, const char* data, size_t remaining)
{
	while(remaining > 0)
	{
		auto bytes_written = write(fd, data, remaining);
		if(bytes_written < 0)
			return(false);
		data += bytes_written;
		remaining -= bytes_written;
	}
	return(true);
}

}

bool file_put_contents_locked_fd(int fd, String content)
{
	if(fd == -1)
		return(false);
	lseek(fd, 0, SEEK_SET);
	if(ftruncate(fd, 0) != 0)
		return(false);
	if(!file_write_all(fd, content.data(), content.length()))
		return(false);
	return(true);
}

String file_get_contents(String file_name)
{
	s32 fd = file_open_locked(file_name, O_RDONLY, LOCK_SH);
	if(fd == -1)
	{
		printf("(!) Could not read %s\n", file_name.c_str());
		return("");
	}
	String content = file_get_contents_locked_fd(fd);
	file_close_locked(fd);
	return(content);
}

bool file_put_contents(String file_name, String content)
{
	s32 fd = file_open_locked(file_name, O_RDWR | O_CREAT, LOCK_EX, 0644);
	if(fd == -1)
	{
		printf("(!) Could not write %s\n", file_name.c_str());
		return(false);
	}
	bool ok = file_put_contents_locked_fd(fd, content);
	file_close_locked(fd);
	if(!ok)
	{
		printf("(!) Could not fully write %s\n", file_name.c_str());
		return(false);
	}
	return(true);
}

bool file_append_contents(String file_name, String content)
{
	s32 fd = file_open_locked(file_name, O_RDWR | O_CREAT, LOCK_EX, 0644);
	if(fd == -1)
	{
		printf("(!) Could not append %s\n", file_name.c_str());
		return(false);
	}
	lseek(fd, 0, SEEK_END);
	bool ok = file_write_all(fd, content.data(), content.length());
	file_close_locked(fd);
	if(!ok)
	{
		printf("(!) Could not fully append %s\n", file_name.c_str());
		return(false);
	}
	return(true);
}

String cwd_get()
{
	return(std::filesystem::current_path());
}

void cwd_set(String path)
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

void file_unlink(String file_name)
{
	remove(file_name.c_str());
}

String expand_path(String path, String relative_to_path)
{
	String result;

	if(relative_to_path == "")
		relative_to_path = cwd_get();

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

f64 time_precise()
{
	return ((f64)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count()) / 1000000;
}

u64 time()
{
	return(std::time(0));
}

String time_format_local(String format, u64 timestamp)
{
	return(time_format_shell(format, timestamp, false));
}

String time_format_utc(String format, u64 timestamp)
{
	if(format == "RFC1123")
		format = "%a, %d %b %Y %T GMT";
	return(time_format_shell(format, timestamp, true));
}

String time_format_relative(u64 timestamp, String format_very_recent, u64 medium_recency_seconds, String format_medium_recent, u64 not_recent_seconds, String format_not_recent)
{
	u64 now_timestamp = time();
	u64 delta_seconds = 0;
	if(now_timestamp > timestamp)
		delta_seconds = now_timestamp - timestamp;

	format_very_recent = first(format_very_recent, "just now");
	medium_recency_seconds = (medium_recency_seconds > 0 ? medium_recency_seconds : 90);
	format_medium_recent = first(format_medium_recent, "%deltaM minutes ago");
	not_recent_seconds = (not_recent_seconds > 0 ? not_recent_seconds : 90 * 60);
	format_not_recent = first(format_not_recent, "%deltaH hours ago");

	if(delta_seconds < medium_recency_seconds)
		return(time_format_expand_delta_tokens(format_very_recent, timestamp, now_timestamp));
	if(delta_seconds < not_recent_seconds)
		return(time_format_expand_delta_tokens(format_medium_recent, timestamp, now_timestamp));
	return(time_format_expand_delta_tokens(format_not_recent, timestamp, now_timestamp));
}

u64 time_parse(String time_String)
{
	return(int_val(trim(shell_exec("date -u -d "+shell_escape(time_String)+" +'%s'"))));
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
	String trace = capture_backtrace_string(32, 1);
	String sig_label = signal_name(sig);
	if(sig_label != "")
		fprintf(stderr, "SEG FAULT: %d (%s):\n%s", sig, sig_label.c_str(), trace.c_str());
	else
		fprintf(stderr, "SEG FAULT: %d:\n%s", sig, trace.c_str());
	exit(1);
}

struct Worker {
	pid_t pid;
};

std::map<pid_t, Worker> workers;
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/prctl.h>

pid_t spawn_subprocess(std::function<void()> exec_after_spawn)
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
		return(0);
	}
	else
	{
		Worker w;
		w.pid = p;
		workers[w.pid] = w;
		printf("(P) child procress spawned: PID %i\n", p);
		return(p);
	}
}

int task_kill(pid_t pid, int sig)
{
	return(kill(pid, sig));
}

pid_t task_pid(String key)
{
	String status_file_name = context->server->config["BIN_DIRECTORY"] + "/task-" + key;
	String lock_file_name = status_file_name + ".lock";
	int lock_fd = file_open_locked(lock_file_name, O_RDWR | O_CREAT, LOCK_EX, 0644);
	String status_file = file_get_contents(status_file_name);
	pid_t p = 0;
	if(status_file != "")
	{
		p = int_val(status_file);
		if(task_kill(p, 0) == 0) // process is still running
		{
			file_close_locked(lock_fd);
			return(p);
		}
		file_unlink(status_file_name);
	}
	file_close_locked(lock_fd);
	return(p);
}

pid_t task(String key, std::function<void()> exec_after_spawn, u64 timeout)
{
	String status_file_name = context->server->config["BIN_DIRECTORY"] + "/task-" + key;
	String lock_file_name = status_file_name + ".lock";
	int lock_fd = file_open_locked(lock_file_name, O_RDWR | O_CREAT, LOCK_EX, 0644);
	String status_file = file_get_contents(status_file_name);
	pid_t p;
	if(status_file != "")
	{
		p = int_val(status_file);
		if(task_kill(p, 0) == 0) // process is still running
		{
			printf("(P) worker process '%s' already running: PID %i\n", key.c_str(), p);
			file_close_locked(lock_fd);
			return(p);
		}
		//printf("(P) worker process '%s' had crashed: PID %i\n", key.c_str(), p);
		file_unlink(status_file_name);
	}
	p = fork();
	if(p == 0)
	{
		file_close_locked(lock_fd);
		my_pid = getpid();

		close(context->resources.client_socket);
		context->resources.client_socket = 0;
		//printf("(C) child procress started, PID:%i\n", my_pid);
		//prctl(PR_SET_PDEATHSIG, SIGHUP);
		exec_after_spawn();
		int exit_lock_fd = file_open_locked(lock_file_name, O_RDWR | O_CREAT, LOCK_EX, 0644);
		file_unlink(status_file_name);
		file_close_locked(exit_lock_fd);
		printf("(P) worker process '%s' terminated: PID %i\n", key.c_str(), my_pid);
		exit(0);
	}
	else
	{
		file_put_contents(status_file_name, std::to_string(p));
		file_close_locked(lock_fd);
		printf("(P) worker process '%s' spawned: PID %i\n", key.c_str(), p);
		return(p);
	}
}

#include <unistd.h>
pid_t task_repeat(String key, f64 interval, std::function<void()> exec_after_spawn, u64 timeout)
{
	auto repeater_function = [&]() {
		while (true)
		{
			exec_after_spawn();
			printf("(P) worker process '%s' sleeping\n", key.c_str());
			usleep((s64)(interval*1000000));
		}
	};
	return(task(key, repeater_function, timeout));
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
	cfg["FCGI_SOCKET_PATH"] = "/run/uce.sock";
	cfg["TMP_UPLOAD_PATH"] = "/tmp/uce/uploads";
	cfg["SESSION_PATH"] = "/tmp/uce/sessions";
	cfg["COMPILER_SYS_PATH"] = ".";
	cfg["PRECOMPILE_FILES_IN"] = "";
	cfg["SITE_DIRECTORY"] = "site";
	cfg["JIT_COMPILE_ON_REQUEST"] = "1";
	cfg["PROACTIVE_COMPILE_ENABLED"] = "1";
	cfg["COMPILE_FAILURE_RETRY_SECONDS"] = std::to_string(10);
	cfg["PROACTIVE_COMPILE_CHECK_INTERVAL"] = std::to_string(60);

	cfg["HTTP_PORT"] = std::to_string(8080);
	cfg["SESSION_TIME"] = std::to_string(60*60*24*30);
	cfg["WORKER_COUNT"] = std::to_string(4);
	cfg["MAX_MEMORY"] = std::to_string(1024*1024*16);

	for(auto& it : split_kv(file_get_contents("/etc/uce/settings.cfg")))
	{
		cfg[it.first] = it.second;
	}

	if(cfg["FCGI_SOCKET_PATH"] == "" && cfg["SOCKET_PATH"] != "")
		cfg["FCGI_SOCKET_PATH"] = cfg["SOCKET_PATH"];
	if(cfg["SOCKET_PATH"] == "" && cfg["FCGI_SOCKET_PATH"] != "")
		cfg["SOCKET_PATH"] = cfg["FCGI_SOCKET_PATH"];

	if(cfg["FCGI_PORT"] == "" && cfg["LISTEN_PORT"] != "")
		cfg["FCGI_PORT"] = cfg["LISTEN_PORT"];
	if(cfg["LISTEN_PORT"] == "" && cfg["FCGI_PORT"] != "")
		cfg["LISTEN_PORT"] = cfg["FCGI_PORT"];

	return(cfg);
}
