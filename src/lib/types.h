#pragma once

#include <string>
#include <map>
#include <list>
#include <vector>
#include <functional>
#include <sstream>
#include <atomic>

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

typedef std::map<String, String> StringMap;
typedef std::vector<String> StringList;
typedef std::ostringstream ByteStream;

struct Request;
struct DTree;

typedef void (*request_ref_handler)(Request& request);
typedef DTree* (*dtree_call_handler)(DTree* call_param);
typedef void (*request_handler)(Request* request);

String to_string(s64 v) { return(std::to_string(v)); }

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
	request_ref_handler on_render;
	request_ref_handler on_component;
	request_ref_handler on_websocket;

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
	StringMap config;
	u32 request_count = 0;

};

struct URI {

	StringMap query;
	StringMap parts;

};

String nibble(String div, String& haystack);

#include "dtree.h"

void compiler_invoke(Request* context, String file_name);

struct Request {

	ServerState* server;

	StringMap params;
	StringMap get;
	StringMap post;
	StringMap cookies;
	StringMap session;

	DTree var;
	DTree cfg;
	DTree call;
	DTree connection;

	String session_id = "";
	String session_name = "";
	std::vector<UploadedFile> uploaded_files;

	String response_code = "HTTP/1.1 200 OK";
	StringMap header;
	StringList set_cookies;

	u64 random_seed;
	u64 random_index;

	std::vector<ByteStream*> ob_stack;
	ByteStream* ob;

	String in;
	String out;
	String err;

	struct Flags {
		bool log_request = true;
		bool is_finished = false;
		int status = 0;
		bool output_closed = false;
		bool params_closed = false;
		bool input_closed = false;
	} flags;

	struct Stats {
		u32 bytes_written;
		f64 time_init;
		f64 time_start;
		f64 time_end;
		u64 mem_high = 0;
		u64 mem_alloc = 0;
		u32 invoke_count = 0;
	} stats;

	// OS-related resources
	struct Resources {
		std::vector<u64> sockets;
		std::vector<void*> mysql_connections;
		u64 client_socket = 0;
		u64 server_socket = 0;
		bool is_websocket = false;
		String websocket_connection_id = "";
		String websocket_scope = "";
		DTree* websocket_connection_state = 0;
		u8 websocket_opcode = 0;
		bool websocket_is_binary = false;
		bool websocket_is_text = false;
		String current_unit_file = "";
		std::string params_buffer;
	} resources;

	void ob_start();
	void set_status(s32 code, String reason = "");

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

template <typename... Ts>
void out(Ts... args)
{
    ((*context->ob << args), ...);
}

template <typename... Ts>
String concat(Ts... args)
{
	ByteStream out;
    ((out << args), ...);
    return(out.str());
}

void * operator new(decltype(sizeof(0)) n) noexcept(false)
{
	void* ptr = malloc(n);
	if(context)
	{
		context->stats.mem_alloc += n;
		if(context->stats.mem_alloc > context->stats.mem_high)
			context->stats.mem_high = context->stats.mem_alloc;
	}
	return(ptr);
}

void operator delete(void * p) throw()
{
	//TO DO: track deallocations
	free(p);
}
