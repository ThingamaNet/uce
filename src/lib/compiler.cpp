#include "compiler.h"
#include <sys/file.h>

String process_text_literal(Request* context, SharedUnit* su, String content)
{
	String pc;
	String HT_START = "print(R\"(";
	String HT_END = ")\");";

	u8 mode = 0;
	char quote_char;
	bool inside_quote = false;
	String code_buffer = "";
	bool is_field = false;
	bool escape_field = false;

	for(u32 i = 0; i < content.length(); i++)
	{
		char c = content[i];
		char c1 = (i + 1 < content.length()) ? content[i + 1] : '\0';
		char c2 = (i + 2 < content.length()) ? content[i + 2] : '\0';

		switch(mode)
		{
			case(0):
				if(c == '<' && c1 == '?')
				{
					code_buffer = "";
					if(c2 == '=')
					{
						is_field = true;
						escape_field = true;
						i += 2;
					}
					else if(c2 == ':')
					{
						is_field = true;
						escape_field = false;
						i += 2;
					}
					else
					{
						is_field = false;
						escape_field = false;
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
					if(quote_char == c && (i == 0 || content[i-1] != '\\'))
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
					else if(c == '?' && c1 == '>')
					{
						mode = 0;
						i += 1;
						if(is_field)
						{
							if(escape_field)
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
								pc.append(
									HT_END +
									"print( " +
									code_buffer +
									" ); " +
									HT_START
								);
							}
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

String preprocess_named_render_syntax(String content)
{
	String result = "";
	String current_line = "";

	auto flush_line = [&]() {
		if(current_line.length() == 0)
			return;

		String line = current_line;
		String line_break = "";
		if(line.length() > 0 && line.back() == '\n')
		{
			line_break = "\n";
			line.pop_back();
		}

		u32 indent_length = 0;
		while(indent_length < line.length() && isspace(line[indent_length]))
			indent_length += 1;

		String indent = line.substr(0, indent_length);
		String trimmed = trim(line);
		if(trimmed.rfind("RENDER:", 0) == 0)
		{
			String signature = trimmed.substr(7);
			auto open_paren_pos = signature.find("(");
			if(open_paren_pos != String::npos)
			{
				String render_name = trim(signature.substr(0, open_paren_pos));
				String render_signature = signature.substr(open_paren_pos);
				if(render_name != "")
					line = indent + "EXPORT void render_" + safe_name(render_name) + render_signature;
			}
		}

		result += line + line_break;
		current_line = "";
	};

	for(auto c : content)
	{
		current_line.append(1, c);
		if(c == '\n')
			flush_line();
	}
	flush_line();

	return(result);
}

String preprocess_shared_unit_char_wise(Request* context, SharedUnit* su, String content)
{
	String pc =
		("#include \"")+context->server->config["COMPILER_SYS_PATH"] +"/src/lib/uce_lib.h\" \n"+
		file_get_contents(
			context->server->config["COMPILER_SYS_PATH"] + "/" + context->server->config["SETUP_TEMPLATE"]
		)+
		"#line 1\n";
	String token = "";
	String html_buffer = "";
	u8 mode = 0;
	bool inside_quote = false;
	u32 source_length = content.length();
	String current_line = "";
	for(u32 i = 0; i < source_length; i++)
	{
		char c = content[i];
		char c1 = (i + 1 < source_length) ? content[i + 1] : '\0';
		char c2 = (i + 2 < source_length) ? content[i + 2] : '\0';
		current_line.append(1, c);
		if(mode == 1)
		{
			if(c == '<' && c1 == '/' && c2 == '>')
			{
				i += 2;
				pc.append(process_text_literal(context, su, html_buffer));
				mode = 0;
			}
			else
			{
				token.append(1, c);
				html_buffer.append(1, c);
			}
		}
		else if(!inside_quote && c == '<' && c1 == '>')
		{
			mode = 1;
			token = "";
			html_buffer = "";
			i += 1;
		}
		else if(c == '\"')
		{
			inside_quote = !inside_quote;
			pc.append(1, c);
		}
		else // we're in C++ code here
		{
			pc.append(1, c);
			if(c == 10 && current_line.substr(0, 6) == "#load ")
			{
				//printf("#load directive\n");
				pc.resize(pc.length() - current_line.length());
				nibble(current_line, "\"");
				String unit_name = nibble(current_line, "\"");
				//printf("(i) #load %s\n", unit_name.c_str());
				SharedUnit* sub_su = compiler_load_shared_unit(context, unit_name, su->src_path, true);
				if(sub_su)
				{
					pc.append("#include \"" + sub_su->bin_path + "/" + sub_su->pre_file_name + "\"\n");
				}
			}
			else
			{
				String trimmed_line = trim(current_line);
				if(c == 10 && trimmed_line.length() > 7 && trimmed_line.substr(0, 6) == "EXPORT" && isspace(trimmed_line[6]))
				{
					current_line = "";
					auto end_declaration_pos = content.find("{", i);
					if(end_declaration_pos != std::string::npos)
					{
						pc.append(1, '\n');
						String declaration = trim(content.substr(i, end_declaration_pos - i));
						su->api_declarations.push_back(declaration+";\n");
						//printf("declaration found: %s\n", declaration.c_str());
					}
				}
			}
		}
		if(c == 10)
			current_line = "";
	}
	return(pc);
}

String preprocess_shared_unit(Request* context, SharedUnit* su)
{
	String content = file_get_contents(su->file_name);
	content = preprocess_named_render_syntax(content);
	return(
		preprocess_shared_unit_char_wise(
			context,
			su,
			content
		)
	);
}

void setup_unit_paths(Request* context, SharedUnit* su, String file_name)
{
	su->file_name = file_name;

	if(su->src_path.length() > 0) // we did this already
		return;

	su->src_path = dirname(file_name);
	su->bin_path = context->server->config["BIN_DIRECTORY"] + su->src_path;
	su->pre_path = context->server->config["BIN_DIRECTORY"] + su->src_path;

	su->src_file_name = basename(file_name);
	su->bin_file_name = su->src_file_name + ".so";
	su->pre_file_name = su->src_file_name + ".cpp";

	su->so_name = su->bin_path + "/" + su->bin_file_name;
	su->api_file_name = su->bin_path + "/" + su->src_file_name + ".exports.txt";
	//su->setup_file_name = su->bin_path + "/" + su->src_file_name + ".setup.h";
}

void load_shared_unit(Request* context, SharedUnit* su, String file_name)
{
	//setup_unit_paths(context, su, file_name);

	su->on_render = 0;
	su->on_websocket = 0;
	su->on_setup = 0;
	su->compiler_messages = "";

	if(!file_exists(su->so_name))
	{
		if(su->opt_so_optional)
			return;
		//printf("(i) unit file not found: %s\n", su->so_name.c_str());
		su->compiler_messages = "unit file not found";
		return;
	}

	su->so_handle = dlopen(su->so_name.c_str(), RTLD_NOW);
	if(su->so_handle)
	{
		su->last_compiled = file_mtime(su->so_name);
		char *error;
		su->on_setup = (request_handler)dlsym(su->so_handle, "set_current_request");
		if ((error = dlerror()) != NULL)
			printf("Error - %s in %s\n", error, su->file_name.c_str());
		su->on_render = (request_ref_handler)dlsym(su->so_handle, "render");
		dlerror();
		su->on_websocket = (request_ref_handler)dlsym(su->so_handle, "websocket");
		dlerror();
		su->api_declarations = split(file_get_contents(su->api_file_name), "\n");
		//else
		//	printf("(i) loaded unit %s\n", su->file_name.c_str());
	}
	else
	{
		printf("Error loading unit %s, could not open %s\n", su->file_name.c_str(), su->so_name.c_str());
	}
}

/*String compile_setup_file(Request* context, SharedUnit* su)
{
	String result =
		String("#ifndef UCE_LIB_INCLUDED\n") +
		"#define UCE_LIB_INCLUDED\n" +
		("#include \"")+context->server->config["COMPILER_SYS_PATH"] +"/src/lib/uce_lib.h\" \n"+
		file_get_contents(
			context->server->config["COMPILER_SYS_PATH"] + "/" + context->server->config["SETUP_TEMPLATE"]) +
		"#endif \n";
	return(result);
}*/

void compile_shared_unit(Request* context, SharedUnit* su, String file_name)
{
	//setup_unit_paths(context, su, file_name);
	f64 comp_start = microtime();

	if(!file_exists(su->file_name))
	{
		su->compiler_messages = "source file not found (" + su->file_name + ")";
		return;
	}

	shell_exec("mkdir -p " + shell_escape(su->pre_path));
	file_put_contents(su->pre_path + "/" + su->pre_file_name, preprocess_shared_unit(context, su));
	//file_put_contents(su->setup_file_name, compile_setup_file(context, su));
	file_put_contents(su->api_file_name, join(su->api_declarations, "\n"));

	if(!su->opt_so_optional)
		su->compiler_messages = trim(shell_exec(context->server->config["COMPILE_SCRIPT"]+" "+
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
		printf("(i) compiled unit %s in %f s\n",
			(su->pre_path + "/" + su->pre_file_name).c_str(),
			microtime() - comp_start);
	}
}

SharedUnit* get_shared_unit(Request* context, String file_name, bool opt_so_optional)
{
	SharedUnit* su = context->server->units[file_name];
	auto mod_time = file_mtime(file_name);
	auto setup_template_time = file_mtime(
		context->server->config["COMPILER_SYS_PATH"] + "/" +
		context->server->config["SETUP_TEMPLATE"]);
	if(setup_template_time > mod_time)
		mod_time = setup_template_time;
	auto compiled_time = su ? file_mtime(su->so_name) : 0;
	bool do_recompile = false;
	if(su && (compiled_time < mod_time || mod_time == 0))
	{
		delete su;
		su = 0;
		do_recompile = true;
	}
	else if(su && (su->last_compiled < mod_time))
	{
		delete su;
		su = 0;
		do_recompile = false;
	}
	if(!su)
	{
		su = new SharedUnit();
		setup_unit_paths(context, su, file_name);
		su->opt_so_optional = opt_so_optional;
		if(compiled_time != 0)
		{
			// if we didn't decide to force recompile yet, we need to check
			// (this case should only happen if the SU cache for that entry is cold
			//  but the SO itself exists _AND_ is stale)
			su->last_compiled = compiled_time;
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

SharedUnit* compiler_load_shared_unit(Request* context, String file_name, String current_path, bool opt_so_optional)
{

	context->stats.invoke_count++;

	if(file_name[0] != '/')
	{
		file_name = expand_path(file_name, current_path);
	}

	//printf("(i) load '%s'\n", file_name.c_str());

	//switch_to_system_alloc();
	auto su = get_shared_unit(context, file_name, opt_so_optional);
	//switch_to_arena(context->mem);
	if(!su)
	{
		printf("Error loading unit %s\n", file_name.c_str());
		print("Error loading unit: "+file_name);
		return(0);
	}
	else if(su->compiler_messages.length() > 0)
	{
		if(context->stats.invoke_count == 1)
			context->header["Content-Type"] = "text/plain";
		print(su->compiler_messages);
		return(0);
	}
	else
	{
		return(su);
	}

}

String component_normalize_path(String name)
{
	name = trim(name);
	if(name.length() >= 4 && name.substr(name.length() - 4) == ".uce")
		return(name);
	return(name + ".uce");
}

void component_parse_target(String target, String& file_name, String& render_name)
{
	target = trim(target);
	render_name = "render";
	auto render_split_pos = target.find(":");
	if(render_split_pos != String::npos)
	{
		render_name = trim(target.substr(render_split_pos + 1));
		target = trim(target.substr(0, render_split_pos));
		if(render_name == "")
			render_name = "render";
	}
	file_name = target;
}

String component_resolve_path(String name)
{
	String file_name;
	String render_name;
	component_parse_target(name, file_name, render_name);

	if(file_name == "")
		return("");

	StringList candidates;
	auto push_candidate = [&] (String candidate) {
		if(candidate == "")
			return;
		candidates.push_back(candidate);
	};

	push_candidate(file_name);
	push_candidate(component_normalize_path(file_name));

	if(file_name.rfind("components/", 0) != 0)
	{
		push_candidate("components/" + file_name);
		push_candidate(component_normalize_path("components/" + file_name));
	}

	std::map<String, bool> seen;
	for(auto& candidate : candidates)
	{
		if(seen[candidate])
			continue;
		seen[candidate] = true;
		String resolved = candidate;
		if(resolved[0] != '/')
			resolved = expand_path(resolved, get_cwd());
		if(file_exists(resolved))
			return(resolved);
	}

	return("");
}

String render_handler_symbol(String render_name)
{
	render_name = trim(render_name);
	if(render_name == "" || render_name == "render")
		return("render");
	return("render_" + safe_name(render_name));
}

request_ref_handler get_render_handler(SharedUnit* su, String render_name)
{
	String symbol = render_handler_symbol(render_name);
	if(symbol == "render")
		return(su->on_render);

	auto it = su->api_functions.find(symbol);
	if(it != su->api_functions.end())
		return((request_ref_handler)it->second);

	auto handler = (request_ref_handler)dlsym(su->so_handle, symbol.c_str());
	dlerror();
	su->api_functions[symbol] = (void*)handler;
	return(handler);
}

bool compiler_invoke_render(Request* context, String file_name, String render_name, String* error_out = 0)
{
	auto su = compiler_load_shared_unit(context, file_name, "", false);
	if(!su)
		return(false);

	if(!su->on_setup)
	{
		if(error_out)
			*error_out = "internal error: set_current_request() not defined in " + file_name;
		return(false);
	}

	auto handler = get_render_handler(su, render_name);
	if(!handler)
	{
		if(error_out)
		{
			if(trim(render_name) == "" || trim(render_name) == "render")
				*error_out = "no RENDER() entry point";
			else
				*error_out = "no RENDER:" + render_name + "() entry point";
		}
		return(false);
	}

	String prev_wd = get_cwd();
	set_cwd(su->src_path);
	su->on_setup(context);
	handler(*context);
	set_cwd(prev_wd);
	return(true);
}

void compiler_invoke(Request* context, String file_name)
{
	printf("(i) compiler_invoke file %s\n", file_name.c_str());
	String error_message = "";
	if(!compiler_invoke_render(context, file_name, "render", &error_message) && error_message != "")
	{
		if(context->stats.invoke_count == 1)
			context->header["Content-Type"] = "text/plain";
		print(error_message);
	}
}

void compiler_invoke_websocket(Request* context, String file_name)
{
	auto su = compiler_load_shared_unit(context, file_name, "", false);
	if(!su)
		return;

	if(!su->on_setup)
	{
		printf("internal error: set_current_request() not defined in %s\n", file_name.c_str());
		return;
	}

	if(!su->on_websocket)
	{
		printf("no WS() entry point in %s\n", file_name.c_str());
		return;
	}

	String prev_wd = get_cwd();
	set_cwd(su->src_path);
	su->on_setup(context);
	su->on_websocket(*context);
	set_cwd(prev_wd);
}

void render_file(String file_name)
{
	//printf("(i) render_file(%s)\n", file_name.c_str());
	compiler_invoke(context, file_name);
}

void render_file(String file_name, Request& context)
{
	compiler_invoke(&context, file_name);
}

String component_resolve(String name)
{
	return(component_resolve_path(name));
}

bool component_exists(String name)
{
	return(component_resolve(name) != "");
}

String component_error_banner(String message)
{
	return("<div class=\"banner\">" + html_escape(message) + "</div>");
}

void render_component(String name)
{
	DTree props;
	render_component(name, props, *context);
}

void render_component(String name, Request& context)
{
	DTree props;
	render_component(name, props, context);
}

void render_component(String name, DTree props)
{
	render_component(name, props, *context);
}

void render_component(String name, DTree props, Request& context)
{
	String file_name;
	String render_name;
	component_parse_target(name, file_name, render_name);

	String resolved_name = component_resolve_path(file_name);
	if(resolved_name == "")
	{
		print(component_error_banner("component not found: " + file_name));
		return;
	}

	DTree previous_call = context.call;
	context.call = props;

	String error_message = "";
	if(!compiler_invoke_render(&context, resolved_name, render_name, &error_message) && error_message != "")
		print(component_error_banner(error_message));

	context.call = previous_call;
}

String component(String name)
{
	DTree props;
	return(component(name, props, *context));
}

String component(String name, Request& context)
{
	DTree props;
	return(component(name, props, context));
}

String component(String name, DTree props)
{
	return(component(name, props, *context));
}

String component(String name, DTree props, Request& context)
{
	ob_start();
	render_component(name, props, context);
	return(ob_get_close());
}

SharedUnit* load_file(String file_name)
{
	auto su = compiler_load_shared_unit(context, file_name, "", false);
	if(su && su->so_handle)
	{
		return(su);
	}
	else
	{
		return(0);
	}
}

DTree* call_file(String file_name, String function_name, DTree* call_param)
{
	DTree* result;
	auto su = compiler_load_shared_unit(context, file_name, "", false);
	if(su && su->so_handle)
	{
		if(!su->on_setup)
		{
			print("internal error: set_current_request() not defined in", file_name, "\n");
		}
		else
		{
			auto f = (dtree_call_handler)dlsym(su->so_handle, function_name.c_str());
			if(!f)
			{
				print("Error: call_file() function '", function_name, "' not found");
			}
			else
			{
				String prev_wd = get_cwd();
				set_cwd(su->src_path);
				su->on_setup(context);
				result = f(call_param);
				set_cwd(prev_wd);
			}
		}
	}
	else
	{
		print("Error: call_file() could not load unit file '", file_name, "'");
	}
	return(result);
}
