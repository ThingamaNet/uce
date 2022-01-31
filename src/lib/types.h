#include <string>
#include <map>
#include <list>
#include <vector>
#include <functional>
#include <sstream>

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef float f32;
typedef double f64;
typedef unsigned long long u64;
typedef long long s64;

typedef std::string String;

String operator+(String lhs, u64 rhs) {
	return(lhs + std::to_string(rhs));
}

String operator+(String lhs, u32 rhs) {
	return(lhs + std::to_string(rhs));
}

String operator+(String lhs, s64 rhs) {
	return(lhs + std::to_string(rhs));
}

String operator+(String lhs, s32 rhs) {
	return(lhs + std::to_string(rhs));
}

String operator+(String lhs, f64 rhs) {
	return(lhs + std::to_string(rhs));
}

String operator+(String lhs, f32 rhs) {
	return(lhs + std::to_string(rhs));
}

#define DEBUG_MEMORY_OFF
#define GLOBAL_ARENA_ALLOCATOR

struct MemoryArena {

	u8* data;
	u64 size = 0;
	u64 capacity = 0;
	String name = "unnamed";

	MemoryArena(u64 cap, String _name = "unnamed")
	{
		name = _name;
		capacity = cap;
		printf("(i) memory arena '%s' created with capacity of %llu bytes\n", name.c_str(), capacity);
		data = (u8*)malloc(cap);
	}

	~MemoryArena()
	{
		free(data);
	}

	void clear()
	{
		#ifdef DEBUG_MEMORY
		printf("(i) memory arena '%s' cleared after high mark of %llu bytes\n", name.c_str(), size);
		#endif
		size = 0;
	}

	void* get(u64 size_needed)
	{
		u64 size_aligned = 8 + (8 * ((size_needed) / 8));
		u8* result = data + size;
		if(size_aligned + size >= capacity)
		{
			printf("(!) memory arena '%s' capacity (%llu) exceeded %llu/%llu + %llu >= %llu\n",
				name.c_str(), capacity, size_needed, size_aligned, size, capacity);
			return(0);
		}
		size += size_aligned;
		#ifdef DEBUG_MEMORY_DETAILED
		printf("(i) memory arena '%s' [+%llu]:%p alloc %llu/%llu bytes\n", name.c_str(), size, result, size_needed, size_aligned);
		#endif
		return(result);
	}

};

MemoryArena* current_memory_arena = 0;

void switch_to_system_alloc()
{
#ifdef GLOBAL_ARENA_ALLOCATOR
	current_memory_arena = 0;
#endif
}

void switch_to_arena(MemoryArena* a)
{
#ifdef GLOBAL_ARENA_ALLOCATOR
	current_memory_arena = a;
#endif
}

#ifdef GLOBAL_ARENA_ALLOCATOR
void * operator new(decltype(sizeof(0)) n) noexcept(false)
{
	if(current_memory_arena)
	{
		return(current_memory_arena->get(n));
	}
	else
	{
		return(malloc(n));
	}
}

void operator delete(void * p) throw()
{
	if(current_memory_arena)
	{

	}
	else
	{
		free(p);
	}
}
#endif

typedef std::map<String, String> StringMap;
typedef std::vector<String> StringList;

struct Request;
struct DTree;

typedef void (*call_handler)(DTree& call_param);
typedef DTree* (*dtree_call_handler)(DTree* call_param);
typedef void (*request_handler)(Request* request);

String to_string(s64 v) { return(std::to_string(v)); }

struct ServerSettings {

	String BIN_DIRECTORY = "/tmp/uce/work";
	String COMPILE_SCRIPT = "scripts/compile";
	String SETUP_TEMPLATE = "scripts/setup.h.template";
	String LIT_ESC = "3d5b5_1";
	String CONTENT_TYPE = "text/html; charset=utf-8";
	String SOCKET_PATH = "/run/uce.sock";
	String TMP_UPLOAD_PATH = "/tmp/uce/uploads";
	String SESSION_PATH = "/tmp/uce/sessions";
	String COMPILER_SYS_PATH = ".";
	String PRECOMPILE_FILES_IN = ".";

	u32 LISTEN_PORT = 9993;
	u64 SESSION_TIME = 60*60*24*30;
	u32 WORKER_COUNT = 4;
	u32 MAX_MEMORY = 1024*1024*16;

};

struct SharedUnit {

	String file_name;
	String so_name;
	String api_file_name;
	String setup_file_name;
	StringList api_declarations;
	std::map<String, void*> api_functions;

	String src_path;
	String bin_path;
	String pre_path;
	String src_file_name;
	String bin_file_name;
	String pre_file_name;

	void* so_handle;

	request_handler on_setup;
	call_handler on_render;

	String compiler_messages;
	time_t last_compiled;

	bool opt_so_optional = false;

	~SharedUnit();
};

struct UploadedFile {
	String file_name;
	String tmp_name;
	u32 size;
};

struct ServerState {

	std::map<String, SharedUnit*> units;
	ServerSettings config;
	u32 request_count = 0;

};

struct URI {

	StringMap query;
	StringMap parts;

};

String nibble(String div, String& haystack);

#include "dtree.h"

void compiler_invoke(Request* context, String file_name, DTree& call_param);

struct Request {

	ServerState* server;

	StringMap params;
	StringMap get;
	StringMap post;
	StringMap cookies;
	StringMap session;

	DTree var;

	String session_id = "";
	String session_name = "";
	std::vector<UploadedFile> uploaded_files;

	StringMap header;
	StringList set_cookies;

	u64 random_seed;
	u64 random_index;

	MemoryArena* mem;

	String in;
	std::vector<std::ostringstream*> ob_stack;
	std::ostringstream* ob;
	String out;
	String err;

	bool is_finished = false;

	struct Flags {
		bool log_request = true;
	} flags;

	struct Stats {
		u32 bytes_written;
		f64 time_init;
		f64 time_start;
		f64 time_end;
		u32 invoke_count = 0;
	} stats;

	struct Resources {
		std::vector<u64> sockets;
		std::vector<void*> mysql_connections;
		u64 fcgi_socket = 0;
	} resources;

	//void invoke(String file_name);
	//void invoke(String file_name, DTree& call_param);

	void ob_start();

	~Request();

};

typedef Request FastCGIRequest;

Request* context;

#include <iostream>

template <typename... Ts>
void print(Ts... args)
{
    ((*context->ob << args), ...);
}

