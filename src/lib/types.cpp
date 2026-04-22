#include <sys/file.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <filesystem>
#include <ctype.h>
#include <fstream>
#include <sys/stat.h>
#include <ctime>
#include <dlfcn.h>
#include <limits.h>
#include <algorithm>
#include <sys/stat.h>
#include <iostream>

#include "types.h"

namespace {

String http_status_reason(s32 code)
{
	switch(code)
	{
		case 200: return("OK");
		case 201: return("Created");
		case 202: return("Accepted");
		case 204: return("No Content");
		case 301: return("Moved Permanently");
		case 302: return("Found");
		case 303: return("See Other");
		case 304: return("Not Modified");
		case 307: return("Temporary Redirect");
		case 308: return("Permanent Redirect");
		case 400: return("Bad Request");
		case 401: return("Unauthorized");
		case 403: return("Forbidden");
		case 404: return("Not Found");
		case 405: return("Method Not Allowed");
		case 409: return("Conflict");
		case 422: return("Unprocessable Content");
		case 429: return("Too Many Requests");
		case 500: return("Internal Server Error");
		case 501: return("Not Implemented");
		case 502: return("Bad Gateway");
		case 503: return("Service Unavailable");
		case 504: return("Gateway Timeout");
		default: return("");
	}
}

}

SharedUnit::~SharedUnit()
{
	if(so_handle)
	{
		dlclose(so_handle);
	}
}

String nibble(String div, String& haystack)
{
	auto pos = haystack.find(div);
	if(pos == String::npos)
	{
		auto result = haystack;
		haystack.clear();
		return(result);
	}
	else
	{
		auto result = haystack.substr(0, pos);
		haystack.erase(0, pos+div.length());
		return(result);
	}
}

void Request::ob_start()
{
	ob_stack.push_back(new ByteStream());
	ob = ob_stack.back();
}

void Request::set_status(s32 code, String reason)
{
	if(reason == "")
		reason = http_status_reason(code);
	String prefix = params["GATEWAY_INTERFACE"] != "" ? "Status: " : "HTTP/1.1 ";
	response_code = prefix + std::to_string(code);
	if(reason != "")
		response_code += " " + reason;
	flags.status = code;
}

Request::~Request()
{
	for(auto* stream : ob_stack)
		delete stream;
	ob_stack.clear();
	ob = 0;
	if(session_lock_fd != -1)
	{
		flock(session_lock_fd, LOCK_UN);
		close(session_lock_fd);
	}
	for(auto& sockfd : resources.sockets)
		close(sockfd);
}
