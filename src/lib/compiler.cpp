#include "compiler.h"
#include <sys/file.h>

String process_html_literal(Request* context, SharedUnit* su, String content)
{
	String pc;
	String HT_START = "print(R\"(";
	String HT_END = ")\");";

	u8 mode = 0;
	char quote_char;
	bool inside_quote = false;
	String code_buffer = "";
	bool is_field = false;

	for(u32 i = 0; i < content.length(); i++)
	{
		char c = content[i];

		switch(mode)
		{
			case(0):
				if(c == '<' && content[i+1] == '?')
				{
					code_buffer = "";
					if(content[i+2] == '=')
					{
						is_field = true;
						i += 2;
					}
					else
					{
						is_field = false;
						i += 1;
					}
					mode = 1; // code-parsing mode
				}
				else
				{
					pc.append(1, c);
				}
				break;
			case(1):
				if(inside_quote)
				{
					if(quote_char == c && content[i-1] != '\\')
						inside_quote = false;
					code_buffer.append(1, c);
				}
				else
				{
					if(c == '\"' || c == '\'')
					{
						inside_quote = true;
						quote_char = c;
						code_buffer.append(1, c);
					}
					else if(c == '?' && content[i+1] == '>')
					{
						mode = 0;
						i += 1;
						if(is_field)
						{
							pc.append(
								HT_END +
								"print(html_escape( " +
								code_buffer +
								" )); " +
								HT_START
							);
						}
						else
						{
							pc.append(HT_END + code_buffer + HT_START);
						}
					}
					else
					{
						code_buffer.append(1, c);
					}
				}
				break;
		}

	}

	return(HT_START + pc + HT_END);
}

String preprocess_shared_unit(Request* context, SharedUnit* su)
{
	String content = file_get_contents(su->file_name);
	printf("(c) compiling with root dir %s\n", context->server->config.COMPILER_SYS_PATH.c_str());
	String pc = ("#include \"")+context->server->config.COMPILER_SYS_PATH +"/src/lib/uce_lib.h\" \n";
	String token = "";
	String html_buffer = "";
	u8 mode = 0;
	bool inside_quote = false;
	for(u32 i = 0; i < content.length(); i++)
	{
		char c = content[i];
		if(mode == 2)
		{
			auto end_pos = content.find(String("</")+token+">", i);
			if(end_pos != String::npos)
			{
				u32 len = token.length() + 3 + end_pos - i;
				html_buffer.append(content.substr(i, len - (token.length() == 0 ? 3 : 0)));
				i += len - 1;
				pc.append(process_html_literal(context, su, html_buffer));
			}
			else
			{
				printf("(!) unterminated HTML literal <%s> in %s", token.c_str(), su->file_name.c_str());
			}
			mode = 0;
		}
		else if(mode == 1)
		{
			if(isspace(c) || c == '>')
			{
				mode = 2;
				if(token.length() > 0)
					html_buffer.append(1, c);
			}
			else
			{
				token.append(1, c);
				html_buffer.append(1, c);
			}
		}
		else if(!inside_quote && c == '<' && (content[i+1] == '>'/* || isalpha(content[i+1])*/))
		{
			mode = 1;
			token = "";
			html_buffer = "";
			if(content[i+1] != '>')
				html_buffer.append(1, c);
		}
		else if(c == '\"')
		{
			inside_quote = !inside_quote;
			pc.append(1, c);
		}
		else
		{
			pc.append(1, c);
		}
	}
	return(pc);
}

void setup_unit_paths(Request* context, SharedUnit* su, String file_name)
{
	su->file_name = file_name;

	if(su->src_path.length() > 0) // we did this already
		return;

	su->src_path = dirname(file_name);
	su->bin_path = context->server->config.BIN_DIRECTORY + su->src_path;
	su->pre_path = context->server->config.BIN_DIRECTORY + su->src_path;

	su->src_file_name = basename(file_name);
	su->bin_file_name = su->src_file_name + ".so";
	su->pre_file_name = su->src_file_name + ".cpp";

	su->so_name = su->bin_path + "/" + su->bin_file_name;
}

void load_shared_unit(Request* context, SharedUnit* su, String file_name)
{
	//setup_unit_paths(context, su, file_name);

	su->on_render = 0;
	su->on_setup = 0;
	su->compiler_messages = "";

	if(!file_exists(su->so_name))
	{
		printf("(i) unit file not found: %s\n", su->so_name.c_str());
		su->compiler_messages = "unit file not found";
		return;
	}

	su->so_handle = dlopen(su->so_name.c_str(), RTLD_NOW);
	if(su->so_handle)
	{
		su->last_compiled = file_mtime(su->so_name);
		char *error;
		su->on_setup = (request_handler)dlsym(su->so_handle, "set_current_request");
		su->on_render = (call_handler)dlsym(su->so_handle, "render");
		if ((error = dlerror()) != NULL)
			printf("Error - %s in %s\n", error, su->file_name.c_str());
		else
			printf("(i) loaded unit %s\n", su->file_name.c_str());
	}
	else
	{
		printf("Error loading unit %s, could not open %s\n", su->file_name.c_str(), su->so_name.c_str());
	}
}

void compile_shared_unit(Request* context, SharedUnit* su, String file_name)
{
	//setup_unit_paths(context, su, file_name);

	if(!file_exists(su->file_name))
	{
		su->compiler_messages = "source file not found (" + su->file_name + ")";
		return;
	}

	shell_exec("mkdir -p " + shell_escape(su->pre_path));
	file_put_contents(su->pre_path + "/" + su->pre_file_name, preprocess_shared_unit(context, su));

	printf("Config.COMPILE_SCRIPT %s\n", String(su->pre_path + "/" + su->pre_file_name).c_str());

	su->compiler_messages = trim(shell_exec(context->server->config.COMPILE_SCRIPT+" "+
		shell_escape(su->src_path)+" "+
		shell_escape(su->bin_path)+" "+
		shell_escape(su->file_name)+" "+
		shell_escape(su->pre_file_name)+" "+
		shell_escape(su->bin_file_name)
	));

	if(su->compiler_messages.length() > 0)
	{
		printf("%s \n", su->compiler_messages.c_str());
	}
	else
	{
		load_shared_unit(context, su, file_name);
		printf("(i) compiled unit %s\n", file_name.c_str());
	}
}

SharedUnit* get_shared_unit(Request* context, String file_name)
{
	SharedUnit* su = context->server->units[file_name];
	auto mod_time = file_mtime(file_name);
	bool do_recompile = false;
	if(su && (su->last_compiled < mod_time || mod_time == 0))
	{
		delete su;
		su = 0;
		do_recompile = true;
	}
	if(!su)
	{
		su = new SharedUnit();
		setup_unit_paths(context, su, file_name);
		if(!do_recompile)
		{
			// if we didn't decide to force recompile yet, we need to check
			// (this case should only happen if the SU cache for that entry is cold
			//  but the SO itself exists _AND_ is stale)
			su->last_compiled = file_mtime(su->so_name);
			if(su->last_compiled < mod_time || mod_time == 0)
				do_recompile = true;
		}

		int fdlock = open((su->so_name+".lock").c_str(), O_RDWR | O_CREAT, 0666 );
		int fl_excl = flock(fdlock, LOCK_EX);

		if(do_recompile)
		{
			compile_shared_unit(context, su, file_name);
		}
		else
		{
			load_shared_unit(context, su, file_name);
			if(!su->so_handle)
				compile_shared_unit(context, su, file_name);
		}

		flock(fdlock, LOCK_UN);
		close(fdlock);
		remove((su->so_name+".lock").c_str());

		context->server->units[file_name] = su;
	}
	return(su);
}

void compiler_invoke(Request* context, String file_name, DTree& call_param)
{

	if(file_name[0] != '/')
	{
		file_name = expand_path(file_name);
	}
	//printf("(i) invoke %s\n", file_name.c_str());

	//printf("(i) invoke(%s)\n", file_name.c_str());
	switch_to_system_alloc();
	auto su = get_shared_unit(context, file_name);
	switch_to_arena(context->mem);
	if(!su)
	{
		printf("Error loading unit %s\n", file_name.c_str());
		print("Error loading unit: "+file_name);
	}
	else if(!su->on_render)
	{
		context->header["Content-Type"] = "text/plain";
		print("Compiler error: "+su->compiler_messages);
	}
	else
	{
		if(su->compiler_messages.length() > 0)
			print(su->compiler_messages);
		else
		{
			String prev_wd = get_cwd();
			set_cwd(su->src_path);
			su->on_setup(context);
			su->on_render(call_param);
			set_cwd(prev_wd);
		}
	}
}



