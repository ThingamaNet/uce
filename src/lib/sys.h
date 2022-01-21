#include <signal.h>

String shell_exec(String cmd);
String shell_escape(String raw);
String basename(String fn);
String dirname(String fn);
bool mkdir(String path);
bool file_exists(String path);
String file_get_contents(String file_name);
bool file_put_contents(String file_name, String content);
#include <fstream>
template <typename... Ts>
bool file_append(String file_name, Ts... args)
{
	std::ofstream fout;
	fout.open(file_name.c_str(), std::ios_base::app);
	((fout << args), ...);
	fout.close();
	return(true);
}
String get_cwd();
void set_cwd(String path);
time_t file_mtime(String file_name);
void unlink(String file_name);
String expand_path(String path);
StringList ls(String dir);

f64 microtime();
u64 time();
String date(String format = "", u64 timestamp = 0);
String gmdate(String format = "", u64 timestamp = 0);
u64 parse_time(String time_String);

u64 socket_connect(String host, short port);
void socket_close(u64 sockfd);
bool socket_write(u64 sockfd, String data);
String socket_read(u64 sockfd, u32 max_length = 1024*128, u32 timeout = 1);

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

pid_t task(String key, std::function<void()> exec_after_spawn, u64 timeout = 60*10);
pid_t task_pid(String key);
