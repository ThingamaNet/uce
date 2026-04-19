#include "compiler.h"
#include <algorithm>
#include <filesystem>
#include <sys/file.h>

namespace {

struct SharedUnitFilesystemState
{
	bool source_exists = false;
	time_t source_time = 0;
	time_t compiled_time = 0;
	time_t setup_template_time = 0;
	time_t required_time = 0;
};

String compiler_registry_file_name(Request* context)
{
	return(context->server->config["BIN_DIRECTORY"] + "/known-uce-files.txt");
}

String compiler_registry_lock_file_name(Request* context)
{
	return(compiler_registry_file_name(context) + ".lock");
}

int compiler_open_lock_file(String file_name)
{
	int fdlock = open(file_name.c_str(), O_RDWR | O_CREAT, 0666);
	if(fdlock == -1)
		printf("(!) Could not open lock file %s\n", file_name.c_str());
	return(fdlock);
}

void compiler_close_lock_file(int fdlock)
{
	if(fdlock == -1)
		return;
	flock(fdlock, LOCK_UN);
	close(fdlock);
}

String compiler_normalize_unit_path(Request* context, String file_name)
{
	file_name = trim(file_name);
	if(file_name == "")
		return("");
	if(file_name[0] != '/')
		file_name = expand_path(file_name, context->server->config["COMPILER_SYS_PATH"]);
	return(file_name);
}

bool compiler_is_known_unit_file(String file_name)
{
	return(file_name.length() >= 4 && file_name.substr(file_name.length() - 4) == ".uce");
}

StringList compiler_normalize_unit_list(Request* context, StringList files)
{
	StringList normalized;
	for(auto& file_name : files)
	{
		auto normalized_name = compiler_normalize_unit_path(context, trim(file_name));
		if(normalized_name != "" && compiler_is_known_unit_file(normalized_name))
			normalized.push_back(normalized_name);
	}
	std::sort(normalized.begin(), normalized.end());
	normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
	return(normalized);
}

StringList compiler_read_known_units_unlocked(Request* context)
{
	auto content = trim(file_get_contents(compiler_registry_file_name(context)));
	if(content == "")
		return(StringList());
	return(compiler_normalize_unit_list(context, split(content, "\n")));
}

void compiler_write_known_units_unlocked(Request* context, StringList files)
{
	files = compiler_normalize_unit_list(context, files);
	if(files.size() == 0)
	{
		file_put_contents(compiler_registry_file_name(context), "");
		return;
	}
	file_put_contents(compiler_registry_file_name(context), join(files, "\n") + "\n");
}

template <typename TCallback>
auto compiler_with_registry_lock(Request* context, TCallback callback) -> decltype(callback())
{
	auto lock_file_name = compiler_registry_lock_file_name(context);
	int fdlock = compiler_open_lock_file(lock_file_name);
	if(fdlock != -1)
		flock(fdlock, LOCK_EX);
	auto result = callback();
	compiler_close_lock_file(fdlock);
	return(result);
}

SharedUnitFilesystemState inspect_shared_unit_filesystem(Request* context, SharedUnit* su)
{
	SharedUnitFilesystemState state;
	state.source_exists = file_exists(su->file_name);
	if(state.source_exists)
		state.source_time = file_mtime(su->file_name);
	state.setup_template_time = file_mtime(
		context->server->config["COMPILER_SYS_PATH"] + "/" +
		context->server->config["SETUP_TEMPLATE"]);
	state.required_time = std::max(state.source_time, state.setup_template_time);
	state.compiled_time = file_mtime(su->so_name);
	return(state);
}

bool shared_unit_cache_is_stale(Request* context, SharedUnit* su)
{
	if(!su)
		return(true);

	auto state = inspect_shared_unit_filesystem(context, su);
	if(!state.source_exists)
		return(true);
	if(state.compiled_time == 0)
		return(true);
	if(state.compiled_time < state.required_time)
		return(true);
	if(su->last_compiled != 0 && state.compiled_time != su->last_compiled)
		return(true);
	return(false);
}

void release_shared_unit_cache_entry(Request* context, String file_name)
{
	auto it = context->server->units.find(file_name);
	if(it == context->server->units.end())
		return;
	delete it->second;
	context->server->units.erase(it);
}

String compiler_current_unit_path(Request* context)
{
	if(!context)
		return("");
	return(first(
		context->resources.current_unit_file,
		context->params["SCRIPT_FILENAME"]
	));
}

String compiler_resolve_unit_path(Request* context, String file_name, String current_path = "")
{
	if(!context)
		return("");

	file_name = trim(file_name);
	if(file_name == "")
		file_name = compiler_current_unit_path(context);

	if(file_name == "")
		return("");

	if(current_path == "")
	{
		auto current_unit_file = compiler_current_unit_path(context);
		if(current_unit_file != "")
			current_path = dirname(current_unit_file);
		else
			current_path = cwd_get();
	}

	if(file_name[0] != '/')
		file_name = expand_path(file_name, current_path);

	return(compiler_normalize_unit_path(context, file_name));
}

f64 compiler_average(f64 total, u64 count)
{
	if(count == 0)
		return(0);
	return(total / (f64)count);
}

void compiler_record_compile_result(SharedUnit* su, f64 duration, bool success, String status, String error_status = "")
{
	su->compile_count += 1;
	su->last_compile_duration = duration;
	su->total_compile_duration += duration;
	if(su->best_compile_duration == 0 || duration < su->best_compile_duration)
		su->best_compile_duration = duration;
	if(duration > su->worst_compile_duration)
		su->worst_compile_duration = duration;

	if(success)
	{
		su->compile_success_count += 1;
		su->compile_status = status;
		su->compile_error_status = "";
	}
	else
	{
		su->compile_failure_count += 1;
		su->compile_status = status;
		su->compile_error_status = error_status;
		su->last_error = time();
	}
}

void compiler_begin_render_result(SharedUnit* su, bool count_request)
{
	su->invoke_count += 1;
	if(count_request)
		su->request_count += 1;
	su->last_rendered = time();
}

void compiler_record_render_result(SharedUnit* su, f64 duration, bool success, String error_status = "")
{
	su->last_render_duration = duration;
	su->total_render_duration += duration;
	if(su->best_render_duration == 0 || duration < su->best_render_duration)
		su->best_render_duration = duration;
	if(duration > su->worst_render_duration)
		su->worst_render_duration = duration;

	if(success)
	{
		su->runtime_error_status = "";
	}
	else
	{
		su->runtime_error_count += 1;
		su->runtime_error_status = error_status;
		su->last_error = time();
	}
}

String compiler_error_status(SharedUnit* su)
{
	if(!su)
		return("");
	if(trim(su->compile_error_status) != "")
		return(su->compile_error_status);
	return(su->runtime_error_status);
}

String compiler_status_from_filesystem(const SharedUnitFilesystemState& state, SharedUnit* su = 0)
{
	if(!state.source_exists)
		return("missing_source");
	if(state.compiled_time == 0)
		return("not_compiled");
	if(state.compiled_time < state.required_time)
		return("stale");
	if(su && su->so_handle)
		return("loaded");
	return("compiled");
}

StringList compiler_unit_exports(SharedUnit* su)
{
	StringList exports;
	if(su && su->api_declarations.size() > 0)
		exports = su->api_declarations;
	for(auto it = exports.begin(); it != exports.end();)
	{
		*it = trim(*it);
		if(*it == "")
		{
			it = exports.erase(it);
			continue;
		}
		++it;
	}
	return(exports);
}

String compiler_unit_exports_text(SharedUnit* su)
{
	if(!su || su->api_file_name == "")
		return("");
	return(trim(file_get_contents(su->api_file_name)));
}

void compiler_tree_push_string(DTree& tree, String value)
{
	DTree item;
	item = value;
	tree.push(item);
}

void compiler_tree_push_strings(DTree& tree, StringList values)
{
	tree.set_array();
	for(auto& value : values)
		compiler_tree_push_string(tree, value);
}

void compiler_tree_set_bool(DTree& tree, String key, bool value)
{
	tree[key].set_bool(value);
}

bool compiler_is_request_entry_unit(Request* context, SharedUnit* su)
{
	if(!context || !su)
		return(false);
	auto request_file = compiler_normalize_unit_path(context, context->params["SCRIPT_FILENAME"]);
	return(request_file != "" && request_file == su->file_name);
}

bool compiler_can_write_response(Request* context)
{
	return(
		context &&
		context->params["REQUEST_METHOD"] != "" &&
		context->ob &&
		context->ob_stack.size() > 0
	);
}

}

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
					else if(c == '<' && c1 == '>')
					{
						// Nested <> markup block inside code
						String sub_buffer = "";
						u32 sub_depth = 0;
						u32 j = i + 2;
						while(j < content.length())
						{
							char jc = content[j];
							char jc1 = (j + 1 < content.length()) ? content[j + 1] : '\0';
							char jc2 = (j + 2 < content.length()) ? content[j + 2] : '\0';
							if(jc == '<' && jc1 == '/' && jc2 == '>')
							{
								if(sub_depth > 0)
								{
									sub_depth--;
									sub_buffer.append("</>");
									j += 3;
								}
								else
								{
									j += 3;
									break;
								}
							}
							else if(jc == '<' && jc1 == '>')
							{
								sub_depth++;
								sub_buffer.append("<>");
								j += 2;
							}
							else
							{
								sub_buffer.append(1, jc);
								j++;
							}
						}
						i = j - 1; // -1 because outer for loop increments
						code_buffer.append(process_text_literal(context, su, sub_buffer));
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
					line = indent + "EXPORT void __unit_render_" + safe_name(render_name) + render_signature;
			}
		}
		else if(trimmed.rfind("COMPONENT:", 0) == 0)
		{
			String signature = trimmed.substr(10);
			auto open_paren_pos = signature.find("(");
			if(open_paren_pos != String::npos)
			{
				String render_name = trim(signature.substr(0, open_paren_pos));
				String render_signature = signature.substr(open_paren_pos);
				if(render_name != "")
					line = indent + "EXPORT void __unit_component_" + safe_name(render_name) + render_signature;
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
	u32 depth = 0;
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
				if(depth > 0)
				{
					depth -= 1;
					token.append("</>");
					html_buffer.append("</>");
					i += 2;
				}
				else
				{
					i += 2;
					pc.append(process_text_literal(context, su, html_buffer));
					mode = 0;
				}
			}
			else if(c == '<' && c1 == '>')
			{
				depth += 1;
				token.append("<>");
				html_buffer.append("<>");
				i += 1;
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
	su->on_component = 0;
	su->on_websocket = 0;
	su->on_setup = 0;
	su->so_handle = 0;
	su->compiler_messages = "";

	if(!file_exists(su->so_name))
	{
		if(su->opt_so_optional)
		{
			su->compile_status = "not_compiled";
			return;
		}
		//printf("(i) unit file not found: %s\n", su->so_name.c_str());
		su->compiler_messages = "unit file not found";
		su->compile_status = "not_compiled";
		su->compile_error_status = su->compiler_messages;
		su->last_error = time();
		return;
	}

	su->so_handle = dlopen(su->so_name.c_str(), RTLD_NOW);
	if(su->so_handle)
	{
		su->last_compiled = file_mtime(su->so_name);
		su->last_loaded = time();
		su->compile_status = "loaded";
		su->compile_error_status = "";
		char *error;
		su->on_setup = (request_handler)dlsym(su->so_handle, "set_current_request");
		if ((error = dlerror()) != NULL)
			printf("Error - %s in %s\n", error, su->file_name.c_str());
		su->on_render = (request_ref_handler)dlsym(su->so_handle, "__unit_render");
		dlerror();
		su->on_component = (request_ref_handler)dlsym(su->so_handle, "__unit_component");
		dlerror();
		su->on_websocket = (request_ref_handler)dlsym(su->so_handle, "__unit_websocket");
		dlerror();
		su->api_declarations = split(file_get_contents(su->api_file_name), "\n");
		//else
		//	printf("(i) loaded unit %s\n", su->file_name.c_str());
	}
	else
	{
		su->compiler_messages = "could not open " + su->so_name;
		su->compile_status = "load_error";
		su->compile_error_status = su->compiler_messages;
		su->last_error = time();
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
	f64 comp_start = time_precise();

	if(!file_exists(su->file_name))
	{
		su->compiler_messages = "source file not found (" + su->file_name + ")";
		compiler_untrack_known_unit(context, su->file_name);
		compiler_record_compile_result(su, time_precise() - comp_start, false, "missing_source", su->compiler_messages);
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
		compiler_record_compile_result(su, time_precise() - comp_start, false, "compile_error", su->compiler_messages);
		printf("%s \n", su->compiler_messages.c_str());
	}
	else
	{
		load_shared_unit(context, su, file_name);
		compiler_record_compile_result(
			su,
			time_precise() - comp_start,
			(su->so_handle != 0),
			(su->so_handle ? "loaded" : "load_error"),
			compiler_error_status(su)
		);
		printf("(i) compiled unit %s in %f s\n",
			(su->pre_path + "/" + su->pre_file_name).c_str(),
			time_precise() - comp_start);
	}
}

SharedUnit* compiler_get_shared_unit_internal(Request* context, String file_name, bool opt_so_optional, bool force_recompile)
{
	file_name = compiler_normalize_unit_path(context, file_name);

	auto existing = context->server->units.find(file_name);
	bool cache_mode_matches = (
		existing != context->server->units.end() &&
		existing->second->opt_so_optional == opt_so_optional
	);
	if(!force_recompile && existing != context->server->units.end() && cache_mode_matches && !shared_unit_cache_is_stale(context, existing->second))
		return(existing->second);

	if(existing != context->server->units.end())
		release_shared_unit_cache_entry(context, file_name);

	SharedUnit* su = new SharedUnit();
	setup_unit_paths(context, su, file_name);
	su->opt_so_optional = opt_so_optional;

	int fdlock = compiler_open_lock_file(su->so_name + ".lock");
	if(fdlock != -1)
		flock(fdlock, LOCK_EX);

	existing = context->server->units.find(file_name);
	cache_mode_matches = (
		existing != context->server->units.end() &&
		existing->second->opt_so_optional == opt_so_optional
	);
	if(existing != context->server->units.end() && (force_recompile || !cache_mode_matches || shared_unit_cache_is_stale(context, existing->second)))
		release_shared_unit_cache_entry(context, file_name);
	existing = context->server->units.find(file_name);
	cache_mode_matches = (
		existing != context->server->units.end() &&
		existing->second->opt_so_optional == opt_so_optional
	);
	if(!force_recompile && existing != context->server->units.end() && cache_mode_matches && !shared_unit_cache_is_stale(context, existing->second))
	{
		compiler_close_lock_file(fdlock);
		delete su;
		return(existing->second);
	}

	auto state = inspect_shared_unit_filesystem(context, su);
	bool do_recompile = force_recompile || !state.source_exists || state.compiled_time == 0 || state.compiled_time < state.required_time;
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

	compiler_close_lock_file(fdlock);

	context->server->units[file_name] = su;
	return(su);
}

SharedUnit* get_shared_unit(Request* context, String file_name, bool opt_so_optional)
{
	return(compiler_get_shared_unit_internal(context, file_name, opt_so_optional, false));
}

SharedUnit* compiler_load_shared_unit(Request* context, String file_name, String current_path, bool opt_so_optional)
{

	context->stats.invoke_count++;

	file_name = compiler_resolve_unit_path(context, file_name, current_path);
	if(file_name == "")
		return(0);
	if(file_exists(file_name))
		compiler_track_known_unit(context, file_name);
	else
		compiler_untrack_known_unit(context, file_name);

	//printf("(i) load '%s'\n", file_name.c_str());

	//switch_to_system_alloc();
	auto su = get_shared_unit(context, file_name, opt_so_optional);
	//switch_to_arena(context->mem);
	if(!su)
	{
		printf("Error loading unit %s\n", file_name.c_str());
		if(compiler_can_write_response(context))
			print("Error loading unit: " + file_name);
		return(0);
	}
	else if(su->compiler_messages.length() > 0)
	{
		printf("%s\n", su->compiler_messages.c_str());
		if(compiler_can_write_response(context) && context->stats.invoke_count == 1)
			context->header["Content-Type"] = "text/plain";
		if(compiler_can_write_response(context))
			print(su->compiler_messages);
		return(0);
	}
	else
	{
		return(su);
	}

}

String compiler_site_directory(Request* context)
{
	String site_directory = first(
		context->server->config["PRECOMPILE_FILES_IN"],
		context->server->config["SITE_DIRECTORY"],
		"site"
	);
	return(compiler_normalize_unit_path(context, site_directory));
}

StringList compiler_scan_site_units(Request* context)
{
	StringList files;
	auto site_directory = compiler_site_directory(context);
	if(site_directory == "" || !file_exists(site_directory))
		return(files);

	std::error_code walk_error;
	auto options = std::filesystem::directory_options::skip_permission_denied;
	for(auto it = std::filesystem::recursive_directory_iterator(site_directory, options, walk_error);
		it != std::filesystem::recursive_directory_iterator();
		it.increment(walk_error))
	{
		if(walk_error)
		{
			printf("(!) proactive scan warning in %s: %s\n",
				site_directory.c_str(),
				walk_error.message().c_str());
			walk_error.clear();
			continue;
		}

		std::error_code entry_error;
		if(!it->is_regular_file(entry_error))
			continue;
		if(entry_error)
			continue;
		auto path = it->path().string();
		if(path.length() >= 4 && path.substr(path.length() - 4) == ".uce")
			files.push_back(path);
	}
	return(compiler_normalize_unit_list(context, files));
}

StringList compiler_list_known_units(Request* context)
{
	auto files = compiler_with_registry_lock(context, [&]() {
		return(compiler_read_known_units_unlocked(context));
	});
	context->server->known_unit_files.clear();
	for(auto& file_name : files)
		context->server->known_unit_files[file_name] = true;
	return(files);
}

void compiler_set_known_units(Request* context, StringList files)
{
	files = compiler_normalize_unit_list(context, files);
	compiler_with_registry_lock(context, [&]() {
		compiler_write_known_units_unlocked(context, files);
		return(0);
	});
	context->server->known_unit_files.clear();
	for(auto& file_name : files)
		context->server->known_unit_files[file_name] = true;
}

void compiler_track_known_unit(Request* context, String file_name)
{
	file_name = compiler_normalize_unit_path(context, file_name);
	if(file_name == "" || !compiler_is_known_unit_file(file_name) || context->server->known_unit_files[file_name])
		return;

	compiler_with_registry_lock(context, [&]() {
		auto files = compiler_read_known_units_unlocked(context);
		files.push_back(file_name);
		compiler_write_known_units_unlocked(context, files);
		return(0);
	});
	context->server->known_unit_files[file_name] = true;
}

void compiler_untrack_known_unit(Request* context, String file_name)
{
	file_name = compiler_normalize_unit_path(context, file_name);
	if(file_name == "")
		return;

	compiler_with_registry_lock(context, [&]() {
		auto files = compiler_read_known_units_unlocked(context);
		files.erase(std::remove(files.begin(), files.end(), file_name), files.end());
		compiler_write_known_units_unlocked(context, files);
		return(0);
	});
	context->server->known_unit_files.erase(file_name);
}

bool compiler_unit_needs_recompile(Request* context, String file_name, bool* source_missing)
{
	file_name = compiler_normalize_unit_path(context, file_name);
	SharedUnit su;
	setup_unit_paths(context, &su, file_name);
	auto state = inspect_shared_unit_filesystem(context, &su);
	if(source_missing)
		*source_missing = !state.source_exists;
	if(!state.source_exists)
		return(false);
	if(state.compiled_time == 0)
		return(true);
	return(state.compiled_time < state.required_time);
}

DTree unit_info(String path)
{
	DTree info;
	if(!context)
		return(info);

	String resolved_path = compiler_resolve_unit_path(context, path);
	if(resolved_path == "")
		return(info);

	SharedUnit* su = 0;
	auto it = context->server->units.find(resolved_path);
	if(it != context->server->units.end())
	{
		su = it->second;
	}
	else
	{
		auto known_units = compiler_list_known_units(context);
		if(std::find(known_units.begin(), known_units.end(), resolved_path) == known_units.end() && !file_exists(resolved_path))
			return(info);
	}

	SharedUnit temp_unit;
	if(!su)
	{
		su = &temp_unit;
		setup_unit_paths(context, su, resolved_path);
	}

	auto fs_state = inspect_shared_unit_filesystem(context, su);
	auto exports_text = compiler_unit_exports_text(su);
	auto exports = compiler_unit_exports(su);
	if(exports.size() == 0 && exports_text != "")
		exports = split(exports_text, "\n");

	info["path"] = resolved_path;
	info["file_name"] = su->file_name;
	info["src_path"] = su->src_path;
	info["bin_path"] = su->bin_path;
	info["pre_path"] = su->pre_path;
	info["src_file_name"] = su->src_file_name;
	info["bin_file_name"] = su->bin_file_name;
	info["pre_file_name"] = su->pre_file_name;
	info["so_name"] = su->so_name;
	info["api_file_name"] = su->api_file_name;
	info["compile_status"] = (su->compile_status != "unknown" ? su->compile_status : compiler_status_from_filesystem(fs_state, su));
	info["compile_error_status"] = su->compile_error_status;
	info["runtime_error_status"] = su->runtime_error_status;
	info["error_status"] = compiler_error_status(su);
	info["compiler_messages"] = su->compiler_messages;
	info["last_compiled"] = (f64)su->last_compiled;
	info["last_loaded"] = (f64)su->last_loaded;
	info["last_rendered"] = (f64)su->last_rendered;
	info["last_error"] = (f64)su->last_error;
	info["request_count"] = (f64)su->request_count;
	info["invoke_count"] = (f64)su->invoke_count;
	info["runtime_error_count"] = (f64)su->runtime_error_count;
	info["compile_count"] = (f64)su->compile_count;
	info["compile_success_count"] = (f64)su->compile_success_count;
	info["compile_failure_count"] = (f64)su->compile_failure_count;
	info["last_compile_time"] = su->last_compile_duration;
	info["average_compile_time"] = compiler_average(su->total_compile_duration, su->compile_count);
	info["best_compile_time"] = su->best_compile_duration;
	info["worst_compile_time"] = su->worst_compile_duration;
	info["last_render_time"] = su->last_render_duration;
	info["average_render_time"] = compiler_average(su->total_render_duration, su->invoke_count);
	info["best_render_time"] = su->best_render_duration;
	info["worst_render_time"] = su->worst_render_duration;
	info["source_mtime"] = (f64)fs_state.source_time;
	info["compiled_mtime"] = (f64)fs_state.compiled_time;
	info["setup_template_mtime"] = (f64)fs_state.setup_template_time;
	info["required_mtime"] = (f64)fs_state.required_time;
	compiler_tree_set_bool(info, "known", context->server->known_unit_files[resolved_path]);
	compiler_tree_set_bool(info, "current_unit", resolved_path == compiler_current_unit_path(context));
	compiler_tree_set_bool(info, "loaded", su->so_handle != 0);
	compiler_tree_set_bool(info, "source_exists", fs_state.source_exists);
	compiler_tree_set_bool(info, "compiled_exists", fs_state.compiled_time != 0);
	compiler_tree_set_bool(info, "stale", fs_state.source_exists && fs_state.compiled_time != 0 && fs_state.compiled_time < fs_state.required_time);
	compiler_tree_set_bool(info, "has_render", su->on_render != 0);
	compiler_tree_set_bool(info, "has_component", su->on_component != 0);
	compiler_tree_set_bool(info, "has_websocket", su->on_websocket != 0);
	compiler_tree_set_bool(info, "has_setup", su->on_setup != 0);
	compiler_tree_set_bool(info, "has_error", trim(compiler_error_status(su)) != "");

	compiler_tree_push_strings(info["exports"], exports);
	info["exports_text"] = exports_text;

	return(info);
}

StringList units_list()
{
	if(!context)
		return(StringList());

	auto known_units = compiler_list_known_units(context);
	for(auto& it : context->server->units)
		known_units.push_back(it.first);
	return(compiler_normalize_unit_list(context, known_units));
}

bool unit_compile(String path)
{
	if(!context)
		return(false);

	String resolved_path = compiler_resolve_unit_path(context, path);
	if(resolved_path == "")
		return(false);

	compiler_track_known_unit(context, resolved_path);
	auto su = compiler_get_shared_unit_internal(context, resolved_path, false, true);
	return(su && trim(su->compiler_messages) == "" && su->so_handle != 0);
}

namespace {

struct UnitInvocationScope
{
	Request* context = 0;
	String previous_working_directory;
	String previous_unit_file;

	UnitInvocationScope(Request* context, SharedUnit* su)
	{
		this->context = context;
		previous_working_directory = cwd_get();
		previous_unit_file = context->resources.current_unit_file;
		cwd_set(su->src_path);
		context->resources.current_unit_file = su->file_name;
	}

	~UnitInvocationScope()
	{
		if(!context)
			return;
		context->resources.current_unit_file = previous_unit_file;
		cwd_set(previous_working_directory);
	}
};

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
	render_name = "";
	auto render_split_pos = target.find(":");
	if(render_split_pos != String::npos)
	{
		render_name = trim(target.substr(render_split_pos + 1));
		target = trim(target.substr(0, render_split_pos));
	}
	file_name = target;
}

String component_resolve_path(String name, Request* request_context = 0)
{
	String target_name = trim(name);
	String file_name;
	String render_name;
	component_parse_target(target_name, file_name, render_name);

	if(file_name == "")
	{
		if(target_name.rfind(":", 0) == 0 && request_context && request_context->resources.current_unit_file != "")
			file_name = request_context->resources.current_unit_file;
		else
			return("");
	}

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
			resolved = expand_path(resolved, cwd_get());
		if(file_exists(resolved))
			return(resolved);
	}

	return("");
}

String page_render_handler_symbol(String render_name)
{
	render_name = trim(render_name);
	if(render_name == "" || render_name == "render")
		return("__unit_render");
	return("__unit_render_" + safe_name(render_name));
}

String component_handler_symbol(String render_name)
{
	render_name = trim(render_name);
	if(render_name == "")
		return("__unit_component");
	return("__unit_component_" + safe_name(render_name));
}

request_ref_handler get_page_render_handler(SharedUnit* su, String render_name)
{
	String symbol = page_render_handler_symbol(render_name);
	if(symbol == "__unit_render")
		return(su->on_render);

	auto it = su->api_functions.find(symbol);
	if(it != su->api_functions.end())
		return((request_ref_handler)it->second);

	auto handler = (request_ref_handler)dlsym(su->so_handle, symbol.c_str());
	dlerror();
	su->api_functions[symbol] = (void*)handler;
	return(handler);
}

request_ref_handler get_component_handler(SharedUnit* su, String render_name)
{
	String symbol = component_handler_symbol(render_name);
	if(symbol == "__unit_component")
		return(su->on_component);

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

	auto handler = get_page_render_handler(su, render_name);
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

	UnitInvocationScope invoke_scope(context, su);
	su->on_setup(context);
	f64 render_start = time_precise();
	bool count_request = compiler_is_request_entry_unit(context, su);
	compiler_begin_render_result(su, count_request);
	try
	{
		handler(*context);
		compiler_record_render_result(su, time_precise() - render_start, true);
	}
	catch(...)
	{
		compiler_record_render_result(
			su,
			time_precise() - render_start,
			false,
			"uncaught exception during render"
		);
		throw;
	}
	return(true);
}

bool compiler_invoke_component(Request* context, String file_name, String render_name, String* error_out = 0)
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

	auto handler = get_component_handler(su, render_name);
	if(!handler)
	{
		if(error_out)
		{
			if(trim(render_name) == "")
				*error_out = "no COMPONENT() entry point";
			else
				*error_out = "no COMPONENT:" + render_name + "() entry point";
		}
		return(false);
	}

	UnitInvocationScope invoke_scope(context, su);
	su->on_setup(context);
	f64 render_start = time_precise();
	compiler_begin_render_result(su, false);
	try
	{
		handler(*context);
		compiler_record_render_result(su, time_precise() - render_start, true);
	}
	catch(...)
	{
		compiler_record_render_result(
			su,
			time_precise() - render_start,
			false,
			"uncaught exception during component render"
		);
		throw;
	}
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

	UnitInvocationScope invoke_scope(context, su);
	su->on_setup(context);
	f64 render_start = time_precise();
	bool count_request = compiler_is_request_entry_unit(context, su);
	compiler_begin_render_result(su, count_request);
	try
	{
		su->on_websocket(*context);
		compiler_record_render_result(su, time_precise() - render_start, true);
	}
	catch(...)
	{
		compiler_record_render_result(
			su,
			time_precise() - render_start,
			false,
			"uncaught exception during websocket handler"
		);
		throw;
	}
}

void unit_render(String file_name)
{
	//printf("(i) unit_render(%s)\n", file_name.c_str());
	compiler_invoke(context, file_name);
}

void unit_render(String file_name, Request& context)
{
	compiler_invoke(&context, file_name);
}

String component_resolve(String name)
{
	return(component_resolve_path(name, context));
}

bool component_exists(String name)
{
	return(component_resolve(name) != "");
}

String component_error_banner(String message)
{
	return("<div class=\"banner\">" + html_escape(message) + "</div>");
}

void component_render(String name)
{
	DTree props;
	component_render(name, props, *context);
}

void component_render(String name, Request& context)
{
	DTree props;
	component_render(name, props, context);
}

void component_render(String name, DTree props)
{
	component_render(name, props, *context);
}

void component_render(String name, DTree props, Request& context)
{
	String file_name;
	String render_name;
	component_parse_target(name, file_name, render_name);

	String resolved_name = component_resolve_path(name, &context);
	if(resolved_name == "")
	{
		print(component_error_banner("component not found: " + trim(name)));
		return;
	}

	DTree previous_call = context.call;
	context.call = props;

	String error_message = "";
	if(!compiler_invoke_component(&context, resolved_name, render_name, &error_message) && error_message != "")
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
	component_render(name, props, context);
	return(ob_get_close());
}

SharedUnit* unit_load(String file_name)
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

DTree* unit_call(String file_name, String function_name, DTree* call_param)
{
	DTree* result = 0;
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
				print("Error: unit_call() function '", function_name, "' not found");
			}
			else
			{
				UnitInvocationScope invoke_scope(context, su);
				su->on_setup(context);
				result = f(call_param);
			}
		}
	}
	else
	{
		print("Error: unit_call() could not load unit file '", file_name, "'");
	}
	return(result);
}
