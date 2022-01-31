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

/*
void Request::invoke(String file_name)
{
	DTree call_param;
	compiler_invoke(this, file_name, call_param);
}

void Request::invoke(String file_name, DTree& call_param)
{
	compiler_invoke(this, file_name, call_param);
}

*/

void Request::ob_start()
{
	ob_stack.push_back(new std::ostringstream());
	ob = ob_stack.back();
}

Request::~Request()
{
	for(auto& sockfd : resources.sockets)
		close(sockfd);
}

