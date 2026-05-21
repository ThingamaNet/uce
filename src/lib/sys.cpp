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
#include "hash.h"

namespace {

constexpr f64 FILE_LOCK_WAIT_TIMEOUT_SECONDS = 3.0;

}

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

int file_open_locked(String file_name, int open_flags, int lock_type, int create_mode, f64 wait_timeout_seconds, String purpose)
{
	(void)wait_timeout_seconds;
	(void)purpose;
	int fd = open(file_name.c_str(), open_flags, create_mode);
	if(fd == -1)
		return(-1);
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	if(flock(fd, lock_type) != 0)
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

void file_release_process_locks(String reason)
{
	(void)reason;
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
	s32 fd = file_open_locked(file_name, O_RDONLY, LOCK_SH, 0644, FILE_LOCK_WAIT_TIMEOUT_SECONDS, "file_get_contents:" + file_name);
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
	s32 fd = file_open_locked(file_name, O_RDWR | O_CREAT, LOCK_EX, 0644, FILE_LOCK_WAIT_TIMEOUT_SECONDS, "file_put_contents:" + file_name);
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
	s32 fd = file_open_locked(file_name, O_RDWR | O_CREAT, LOCK_EX, 0644, FILE_LOCK_WAIT_TIMEOUT_SECONDS, "file_append:" + file_name);
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
		file_release_process_locks("fork child startup");
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

String runtime_safe_key(String key, String label)
{
	key = trim(key);
	if(key == "")
		throw std::runtime_error(label + " cannot be empty");
	return(gen_sha1(key));
}

String task_file_prefix(String key)
{
	return(path_join(context->server->config["BIN_DIRECTORY"], "task-" + runtime_safe_key(key, "task key")));
}

struct TaskStatus {
	pid_t pid = 0;
	String process_start_ticks = "";
};

TaskStatus task_status_parse(String status_file)
{
	TaskStatus status;
	auto lines = split(trim(status_file), "\n");
	if(lines.size() > 0)
		status.pid = (pid_t)int_val(trim(lines[0]));
	if(lines.size() > 1)
		status.process_start_ticks = trim(lines[1]);
	return(status);
}

String task_process_start_ticks(pid_t pid)
{
	if(pid <= 0)
		return("");
	String stat_file_name = "/proc/" + std::to_string(pid) + "/stat";
	int fd = open(stat_file_name.c_str(), O_RDONLY);
	if(fd == -1)
		return("");
	char buffer[4096];
	ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if(bytes_read <= 0)
		return("");
	buffer[bytes_read] = '\0';
	String stat = buffer;
	size_t command_end = stat.rfind(") ");
	if(command_end == String::npos)
		return("");
	String after_command = stat.substr(command_end + 2);
	auto fields = split_space(after_command);
	if(fields.size() <= 19)
		return("");
	return(fields[19]);
}

String task_status_content(pid_t pid)
{
	return(std::to_string(pid) + "\n" + task_process_start_ticks(pid) + "\n");
}

bool task_status_is_alive(TaskStatus status)
{
	if(status.pid <= 0)
		return(false);
	if(kill(status.pid, 0) != 0)
		return(false);
	if(status.process_start_ticks == "")
		return(true);
	return(task_process_start_ticks(status.pid) == status.process_start_ticks);
}

void task_close_inherited_fds()
{
	long max_fd = sysconf(_SC_OPEN_MAX);
	if(max_fd < 0 || max_fd > 65536)
		max_fd = 4096;
	for(int fd = 3; fd < max_fd; fd++)
		close(fd);
}

int task_kill(pid_t pid, int sig)
{
	if(pid <= 0)
	{
		errno = EINVAL;
		return(-1);
	}
	return(kill(pid, sig));
}

pid_t task_pid(String key)
{
	String status_file_name = task_file_prefix(key);
	String lock_file_name = status_file_name + ".lock";
	int lock_fd = file_open_locked(lock_file_name, O_RDWR | O_CREAT, LOCK_EX, 0644, FILE_LOCK_WAIT_TIMEOUT_SECONDS, "task-pid:" + key);
	if(lock_fd == -1)
	{
		fprintf(stderr, "task_pid(): could not lock task key '%s'\n", key.c_str());
		return(0);
	}
	String status_file = file_exists(status_file_name) ? file_get_contents(status_file_name) : "";
	if(status_file != "")
	{
		TaskStatus status = task_status_parse(status_file);
		if(task_status_is_alive(status))
		{
			file_close_locked(lock_fd);
			return(status.pid);
		}
		file_unlink(status_file_name);
	}
	file_close_locked(lock_fd);
	return(0);
}

pid_t task(String key, std::function<void()> exec_after_spawn, u64 timeout)
{
	String status_file_name = task_file_prefix(key);
	String lock_file_name = status_file_name + ".lock";
	int lock_fd = file_open_locked(lock_file_name, O_RDWR | O_CREAT, LOCK_EX, 0644, FILE_LOCK_WAIT_TIMEOUT_SECONDS, "task:" + key);
	if(lock_fd == -1)
	{
		fprintf(stderr, "task(): could not lock task key '%s'\n", key.c_str());
		return(0);
	}
	String status_file = file_exists(status_file_name) ? file_get_contents(status_file_name) : "";
	pid_t p = 0;
	if(status_file != "")
	{
		TaskStatus status = task_status_parse(status_file);
		if(task_status_is_alive(status))
		{
			printf("(P) worker process '%s' already running: PID %i\n", key.c_str(), status.pid);
			file_close_locked(lock_fd);
			return(status.pid);
		}
		file_unlink(status_file_name);
	}
	p = fork();
	if(p < 0)
	{
		fprintf(stderr, "task(): fork failed for key '%s': %s\n", key.c_str(), strerror(errno));
		file_close_locked(lock_fd);
		return(0);
	}
	if(p == 0)
	{
		file_release_process_locks("task child startup");
		file_close_locked(lock_fd);
		my_pid = getpid();
		signal(SIGALRM, SIG_DFL);
		if(timeout > 0)
			alarm(timeout);

		if(context->resources.client_socket > 0)
		{
			close(context->resources.client_socket);
			context->resources.client_socket = 0;
		}
		task_close_inherited_fds();
		exec_after_spawn();
		int exit_lock_fd = file_open_locked(lock_file_name, O_RDWR | O_CREAT, LOCK_EX, 0644, FILE_LOCK_WAIT_TIMEOUT_SECONDS, "task-exit:" + key);
		if(exit_lock_fd != -1)
		{
			file_unlink(status_file_name);
			file_close_locked(exit_lock_fd);
		}
		else
		{
			fprintf(stderr, "task(): could not lock task key '%s' during child cleanup\n", key.c_str());
		}
		printf("(P) worker process '%s' terminated: PID %i\n", key.c_str(), my_pid);
		exit(0);
	}

	if(!file_put_contents(status_file_name, task_status_content(p)))
	{
		fprintf(stderr, "task(): could not write status file for key '%s'; terminating child PID %i\n", key.c_str(), p);
		kill(p, SIGTERM);
		file_close_locked(lock_fd);
		return(0);
	}
	file_close_locked(lock_fd);
	printf("(P) worker process '%s' spawned: PID %i\n", key.c_str(), p);
	return(p);
}

#include <unistd.h>
pid_t task_repeat(String key, f64 interval, std::function<void()> exec_after_spawn, u64 timeout)
{
	if(interval <= 0)
		throw std::runtime_error("task_repeat(): interval must be greater than zero");
	auto repeater_function = [key, interval, exec_after_spawn, timeout]() {
		f64 started_at = time_precise();
		while (timeout == 0 || time_precise() - started_at < (f64)timeout)
		{
			exec_after_spawn();
			f64 elapsed = time_precise() - started_at;
			if(timeout > 0 && elapsed >= (f64)timeout)
				break;
			f64 sleep_seconds = interval;
			if(timeout > 0 && elapsed + sleep_seconds > (f64)timeout)
				sleep_seconds = (f64)timeout - elapsed;
			printf("(P) worker process '%s' sleeping\n", key.c_str());
			if(sleep_seconds > 0)
				usleep((useconds_t)(sleep_seconds * 1000000.0));
		}
	};
	return(task(key, repeater_function, timeout));
}

void on_child_exit(int sig)
{
	(void)sig;
	pid_t pid;
	int status;
	while((pid = waitpid(-1, &status, WNOHANG)) > 0)
	{
		if(workers.count(pid) > 0)
		{
			workers.erase(pid);
			printf("(P) child terminated (PID:%i)\n", pid);
		}
		else
		{
			printf("(P) task child reaped (PID:%i)\n", pid);
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
	cfg["CLI_SOCKET_PATH"] = "/run/uce/cli.sock";
	cfg["TMP_UPLOAD_PATH"] = "/tmp/uce/uploads";
	cfg["SESSION_PATH"] = "/tmp/uce/sessions";
	cfg["COMPILER_SYS_PATH"] = ".";
	cfg["PRECOMPILE_FILES_IN"] = "";
	cfg["SITE_DIRECTORY"] = "site";
	cfg["JIT_COMPILE_ON_REQUEST"] = "1";
	cfg["PROACTIVE_COMPILE_ENABLED"] = "1";
	cfg["COMPILE_FAILURE_RETRY_SECONDS"] = std::to_string(10);
	cfg["PROACTIVE_COMPILE_CHECK_INTERVAL"] = std::to_string(60);
	cfg["TRANSPORT_MAX_CLIENT_CONNECTIONS"] = std::to_string(256);
	cfg["TRANSPORT_MAX_HTTP_HEADER_BYTES"] = std::to_string(16 * 1024);
	cfg["TRANSPORT_MAX_HTTP_BODY_BYTES"] = std::to_string(1024 * 1024);
	cfg["TRANSPORT_MAX_WEBSOCKET_FRAME_BYTES"] = std::to_string(1024 * 1024);
	cfg["TRANSPORT_MAX_WEBSOCKET_MESSAGE_BYTES"] = std::to_string(1024 * 1024);
	cfg["TRANSPORT_MAX_WEBSOCKET_OUTPUT_BYTES"] = std::to_string(4 * 1024 * 1024);
	cfg["TRANSPORT_MAX_RESPONSE_BYTES"] = std::to_string(8 * 1024 * 1024);
	cfg["TRANSPORT_HTTP_REQUEST_TIMEOUT_SECONDS"] = "15";
	cfg["TRANSPORT_CONNECTION_IDLE_TIMEOUT_SECONDS"] = "120";
	cfg["CUSTOM_SERVER_MAX_SERVERS"] = "16";
	cfg["CUSTOM_SERVER_MIN_PORT"] = "1024";
	cfg["CUSTOM_SERVER_MAX_PORT"] = "65535";
	cfg["CUSTOM_SERVER_ALLOW_PUBLIC_BIND"] = "0";
	cfg["CUSTOM_SERVER_UNIX_SOCKET_PREFIX"] = "/tmp/uce/custom-servers/";
	cfg["CUSTOM_SERVER_HANDLER_TIMEOUT_SECONDS"] = "30";
	cfg["CUSTOM_SERVER_UCE_ROOT"] = "";
	cfg["ARCHIVE_MAX_INPUT_BYTES"] = std::to_string(64 * 1024 * 1024);
	cfg["ARCHIVE_MAX_OUTPUT_BYTES"] = std::to_string(64 * 1024 * 1024);
	cfg["ARCHIVE_MAX_ZIP_ENTRIES"] = "4096";

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
