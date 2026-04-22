#pragma once

#include <sys/file.h>
#include <signal.h>
#include <sstream>

String shell_exec(String cmd);
String shell_escape(String raw);
String basename(String fn);
String dirname(String fn);
String path_join(String base, String child);
bool mkdir(String path);
bool file_exists(String path);
int file_open_locked(String file_name, int open_flags, int lock_type = LOCK_SH, int create_mode = 0644);
void file_close_locked(int fd);
String file_get_contents_locked_fd(int fd);
bool file_put_contents_locked_fd(int fd, String content);
String file_get_contents(String file_name);
bool file_put_contents(String file_name, String content);
bool file_append_contents(String file_name, String content);
template <typename... Ts>
bool file_append(String file_name, Ts... args)
{
	std::ostringstream out;
	((out << args), ...);
	return(file_append_contents(file_name, out.str()));
}
String cwd_get();
void cwd_set(String path);
time_t file_mtime(String file_name);
void file_unlink(String file_name);
String expand_path(String path, String relative_to_path = "");
StringList ls(String dir);

f64 time_precise();
u64 time();
String time_format_local(String format = "", u64 timestamp = 0);
String time_format_utc(String format = "", u64 timestamp = 0);
String time_format_relative(u64 timestamp, String format_very_recent = "", u64 medium_recency_seconds = 0, String format_medium_recent = "", u64 not_recent_seconds = 0, String format_not_recent = "");
u64 time_parse(String time_String);

u64 socket_connect(String host, short port);
void socket_close(u64 sockfd);
bool socket_write(u64 sockfd, String data);
String socket_read(u64 sockfd, u32 max_length = 1024*128, u32 timeout = 1);

String ws_message();
String ws_connection_id();
String ws_scope();
u8 ws_opcode();
bool ws_is_binary();
StringList ws_connections(String scope = "");
u64 ws_connection_count(String scope = "");
bool ws_send(String message, bool binary = false, String scope = "");
bool ws_send_to(String connection_id, String message, bool binary = false);
bool ws_close(String connection_id = "");

String capture_backtrace_string(u32 max_frames = 32, u32 skip_frames = 0);
String signal_name(int sig);

String memcache_escape_key(String key);
StringList memcache_escape_keys(StringList keys);
u64 memcache_connect(String host = "127.0.0.1", short port = 11211);
String memcache_command(u64 connection, String command);
bool memcache_set(u64 connection, String key, String value, u64 expires_in = 60*60);
bool memcache_delete(u64 connection, String key);
String memcache_get(u64 connection, String key, String default_value = "");
StringMap memcache_get_multiple(u64 connection, StringList keys);

pid_t parent_pid = 0;
pid_t my_pid = 0;

void on_segfault(int sig);
int task_kill(pid_t pid, int sig = 0);

pid_t task(String key, std::function<void()> exec_after_spawn, u64 timeout = 60*10);
pid_t task_repeat(String key, f64 interval, std::function<void()> exec_after_spawn, u64 timeout = 60*10);
pid_t task_pid(String key);
