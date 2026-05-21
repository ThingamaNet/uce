#include "compiler.h"
#include "compiler-parser.h"
#include "hash.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sys/file.h>
#include <unistd.h>

namespace {

const char* UCE_SETUP_SYMBOL = "__uce_set_current_request";
const char* UCE_RENDER_SYMBOL = "__uce_render";
const char* UCE_COMPONENT_SYMBOL = "__uce_component";
const char* UCE_WEBSOCKET_SYMBOL = "__uce_websocket";
const char* UCE_CLI_SYMBOL = "__uce_cli";
const char* UCE_SERVE_HTTP_SYMBOL = "__uce_serve_http";
const char* UCE_ONCE_SYMBOL = "__uce_once";
const char* UCE_INIT_SYMBOL = "__uce_init";
const u64 UCE_UNIT_ABI_VERSION = 1;

struct SharedUnitFilesystemState
{
	bool source_exists = false;
	bool metadata_exists = false;
	bool compile_output_exists = false;
	bool metadata_parsed = false;
	bool abi_compatible = false;
	bool input_signature_matches = false;
	time_t source_time = 0;
	time_t compiled_time = 0;
	time_t metadata_time = 0;
	time_t compile_output_time = 0;
	time_t setup_template_time = 0;
	time_t compiler_header_time = 0;
	time_t compiler_source_time = 0;
	time_t runtime_binary_time = 0;
	time_t compiler_abi_time = 0;
	time_t required_time = 0;
	u64 metadata_abi_version = 0;
	u64 runtime_abi_version = UCE_UNIT_ABI_VERSION;
	String metadata_content;
	String compile_output_content;
	String metadata_build_token;
	String metadata_input_signature;
	String current_input_signature;
};

struct SharedUnitCompileCheck
{
	bool source_missing = false;
	bool needs_compile = false;
};

void compiler_unload_failed_shared_unit(SharedUnit* su);
bool compiler_run_unit_init(Request* context, SharedUnit* su, String* error_out = 0);
bool compiler_run_unit_once_if_needed(Request* context, SharedUnit* su, String* error_out = 0);

bool compiler_jit_compile_on_request_enabled(Request* context)
{
	if(!context || !context->server)
		return(true);
	return(config_bool("JIT_COMPILE_ON_REQUEST", true));
}

bool compiler_is_u64_string(String value)
{
	value = trim(value);
	if(value == "")
		return(false);
	for(auto c : value)
	{
		if(c < '0' || c > '9')
			return(false);
	}
	return(true);
}

StringMap compiler_parse_unit_metadata(String content)
{
	StringMap metadata;
	content = trim(content);
	if(content == "")
		return(metadata);

	auto lines = split(content, "\n");
	for(auto& raw_line : lines)
	{
		auto line = trim(raw_line);
		if(line == "")
			continue;
		auto split_pos = line.find("=");
		if(split_pos == String::npos)
			continue;
		auto key = trim(line.substr(0, split_pos));
		auto value = trim(line.substr(split_pos + 1));
		if(key != "")
			metadata[key] = value;
	}
	return(metadata);
}

String compiler_unit_input_signature(Request* context, SharedUnit* su)
{
	if(!context || !su || !file_exists(su->file_name))
		return("");

	String setup_template = context->server->config["COMPILER_SYS_PATH"] + "/" + context->server->config["SETUP_TEMPLATE"];
	return(
		gen_sha1(file_get_contents(su->file_name)) + ":" +
		gen_sha1(file_get_contents(setup_template)) + ":" +
		std::to_string(UCE_UNIT_ABI_VERSION)
	);
}

String compiler_unit_build_token()
{
	return(
		std::to_string(getpid()) + ":" +
		std::to_string((u64)(time_precise() * 1000000.0))
	);
}

String compiler_unit_metadata_text(Request* context, SharedUnit* su)
{
	return(
		"format=uce-unit-metadata-v1\n"
		"unit_abi_version=" + std::to_string(UCE_UNIT_ABI_VERSION) + "\n"
		"input_signature=" + compiler_unit_input_signature(context, su) + "\n"
		"build_token=" + compiler_unit_build_token() + "\n"
	);
}

SharedUnitCompileCheck shared_unit_compile_check(const SharedUnitFilesystemState& state)
{
	SharedUnitCompileCheck result;
	result.source_missing = !state.source_exists;
	result.needs_compile =
		!state.source_exists ||
		state.compiled_time == 0 ||
		state.compiled_time < state.required_time ||
		!state.metadata_exists ||
		!state.metadata_parsed ||
		!state.abi_compatible ||
		!state.input_signature_matches;
	return(result);
}

bool compiler_failure_retry_deferred(Request* context, SharedUnit* su, const SharedUnitFilesystemState& state)
{
	if(!context || !su)
		return(false);
	if(!state.compile_output_exists || state.compile_output_time == 0)
		return(false);
	if(state.source_time == 0)
		return(false);
	auto required_failure_inputs_time = std::max({
		state.source_time,
		state.setup_template_time,
		state.compiler_abi_time
	});
	return(state.compile_output_time >= required_failure_inputs_time);
}

String compiler_failure_output_for_state(SharedUnit* su, const SharedUnitFilesystemState& state)
{
	if(!state.compile_output_exists)
		return("");
	auto content = trim(state.compile_output_content);
	if(content != "")
		return(content);
	if(su)
		return(trim(su->compile_error_status));
	return("");
}

void compiler_restore_persisted_failure(SharedUnit* su, const SharedUnitFilesystemState& state, String status = "compile_error")
{
	if(!su)
		return;
	auto message = compiler_failure_output_for_state(su, state);
	if(message == "")
		message = "recent compilation failed";
	su->compiler_messages = message;
	su->compile_status = status;
	su->compile_error_status = message;
	su->last_error = (state.compile_output_time != 0 ? state.compile_output_time : time());
}

time_t compiler_runtime_abi_time(Request* context)
{
	if(!context || !context->server)
		return(0);

	String root = context->server->config["COMPILER_SYS_PATH"];
	return(std::max({
		file_mtime(root + "/src/lib/compiler.h"),
		file_mtime(root + "/src/lib/compiler.cpp"),
		file_mtime(root + "/src/lib/compiler-parser.h"),
		file_mtime(root + "/src/lib/compiler-parser.cpp"),
		file_mtime(root + "/bin/uce_fastcgi.linux.bin")
	}));
}

String compiler_registry_file_name(Request* context)
{
	return(context->server->config["BIN_DIRECTORY"] + "/known-uce-files.txt");
}

String compiler_registry_lock_file_name(Request* context)
{
	return(compiler_registry_file_name(context) + ".lock");
}

int compiler_open_lock_file(String file_name, String purpose)
{
	(void)purpose;
	auto lock_dir = dirname(file_name);
	if(lock_dir != "")
		mkdir(lock_dir);
	int fdlock = file_open_locked(file_name, O_RDWR | O_CREAT, LOCK_EX, 0666);
	if(fdlock == -1)
		printf("(!) Could not open lock file %s\n", file_name.c_str());
	return(fdlock);
}

void compiler_close_lock_file(int fdlock)
{
	file_close_locked(fdlock);
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
	int fdlock = compiler_open_lock_file(lock_file_name, "compiler-registry");
	auto result = callback();
	compiler_close_lock_file(fdlock);
	return(result);
}

bool compiler_has_known_unit_cached(Request* context, String file_name)
{
	if(!context)
		return(false);
	return(compiler_with_registry_lock(context, [&]() {
		auto files = compiler_read_known_units_unlocked(context);
		return(std::find(files.begin(), files.end(), file_name) != files.end());
	}));
}

SharedUnitFilesystemState inspect_shared_unit_filesystem(Request* context, SharedUnit* su)
{
	SharedUnitFilesystemState state;
	state.source_exists = file_exists(su->file_name);
	if(state.source_exists)
		state.source_time = file_mtime(su->file_name);
	state.setup_template_time = file_mtime(
		context->server->config["COMPILER_SYS_PATH"] + "/" +
		context->server->config["SETUP_TEMPLATE"]
	);
	state.compiler_header_time = file_mtime(context->server->config["COMPILER_SYS_PATH"] + "/src/lib/compiler.h");
	state.compiler_source_time = file_mtime(context->server->config["COMPILER_SYS_PATH"] + "/src/lib/compiler.cpp");
	state.runtime_binary_time = file_mtime(context->server->config["COMPILER_SYS_PATH"] + "/bin/uce_fastcgi.linux.bin");
	state.compiler_abi_time = compiler_runtime_abi_time(context);
	state.metadata_time = file_mtime(su->meta_file_name);
	state.compile_output_time = file_mtime(su->compile_output_file_name);
	state.current_input_signature = compiler_unit_input_signature(context, su);
	state.metadata_exists = (state.metadata_time != 0);
	state.compile_output_exists = (state.compile_output_time != 0);
	if(state.metadata_exists)
	{
		state.metadata_content = file_get_contents(su->meta_file_name);
		auto metadata = compiler_parse_unit_metadata(state.metadata_content);
		auto abi_it = metadata.find("unit_abi_version");
		if(abi_it != metadata.end() && compiler_is_u64_string(abi_it->second))
		{
			state.metadata_abi_version = (u64)atoll(abi_it->second.c_str());
			state.metadata_parsed = true;
		}
		auto input_it = metadata.find("input_signature");
		if(input_it != metadata.end())
			state.metadata_input_signature = input_it->second;
		auto build_it = metadata.find("build_token");
		if(build_it != metadata.end())
			state.metadata_build_token = build_it->second;
	}
	if(state.compile_output_exists)
		state.compile_output_content = file_get_contents(su->compile_output_file_name);
	state.abi_compatible = (state.metadata_parsed && state.metadata_abi_version == state.runtime_abi_version);
	state.input_signature_matches = (
		state.metadata_parsed &&
		state.metadata_input_signature != "" &&
		state.current_input_signature != "" &&
		state.metadata_input_signature == state.current_input_signature
	);
	state.required_time = std::max({state.source_time, state.setup_template_time, state.compiler_abi_time});
	state.compiled_time = file_mtime(su->so_name);
	return(state);
}

void compiler_record_observed_filesystem_state(SharedUnit* su, const SharedUnitFilesystemState& state)
{
	if(!su)
		return;
	su->observed_compiled_time = state.compiled_time;
	su->observed_metadata_content = state.metadata_content;
}

bool shared_unit_cache_is_stale(Request* context, SharedUnit* su)
{
	if(!su)
		return(true);

	auto state = inspect_shared_unit_filesystem(context, su);
	if(shared_unit_compile_check(state).needs_compile)
		return(true);
	if(state.compiled_time != su->observed_compiled_time)
		return(true);
	if(state.metadata_content != su->observed_metadata_content)
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

SharedUnit* compiler_cached_unit(Request* context, String file_name)
{
	auto it = context->server->units.find(file_name);
	if(it == context->server->units.end())
		return(0);
	return(it->second);
}

bool compiler_cache_mode_matches(SharedUnit* su, bool opt_so_optional)
{
	return(su && su->opt_so_optional == opt_so_optional);
}

bool compiler_cached_unit_is_reusable(Request* context, SharedUnit* su, bool opt_so_optional, bool force_recompile)
{
	return(
		!force_recompile &&
		compiler_cache_mode_matches(su, opt_so_optional) &&
		!shared_unit_cache_is_stale(context, su)
	);
}

SharedUnit* compiler_reusable_cached_unit(Request* context, String file_name, bool opt_so_optional, bool force_recompile)
{
	auto su = compiler_cached_unit(context, file_name);
	if(compiler_cached_unit_is_reusable(context, su, opt_so_optional, force_recompile))
		return(su);
	return(0);
}

void compiler_release_cached_unit_if_needed(Request* context, String file_name, bool opt_so_optional, bool force_recompile)
{
	auto su = compiler_cached_unit(context, file_name);
	if(!su)
		return;
	if(force_recompile || !compiler_cache_mode_matches(su, opt_so_optional) || shared_unit_cache_is_stale(context, su))
		release_shared_unit_cache_entry(context, file_name);
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
	auto compile_check = shared_unit_compile_check(state);
	if(!state.source_exists)
		return("missing_source");
	if(state.compiled_time == 0 || !state.metadata_exists)
		return("not_compiled");
	if(compile_check.needs_compile)
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

String preprocess_shared_unit(Request* context, SharedUnit* su)
{
	String content = file_get_contents(su->file_name);
	return(compiler_preprocess_source(context, su, content));
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
	su->meta_file_name = su->bin_path + "/" + su->src_file_name + ".meta.txt";
	su->compile_output_file_name = su->bin_path + "/" + su->src_file_name + ".compile.txt";
	//su->setup_file_name = su->bin_path + "/" + su->src_file_name + ".setup.h";
}

void load_shared_unit(Request* context, SharedUnit* su)
{
	su->on_render = 0;
	su->on_component = 0;
	su->on_websocket = 0;
	su->on_once = 0;
	su->on_init = 0;
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
		auto fs_state = inspect_shared_unit_filesystem(context, su);
		if(compiler_failure_retry_deferred(context, su, fs_state))
		{
			compiler_restore_persisted_failure(su, fs_state);
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
		su->on_setup = (request_handler)dlsym(su->so_handle, UCE_SETUP_SYMBOL);
		if ((error = dlerror()) != NULL)
			printf("Error - %s in %s\n", error, su->file_name.c_str());
		su->on_render = (request_ref_handler)dlsym(su->so_handle, UCE_RENDER_SYMBOL);
		dlerror();
		su->on_component = (request_ref_handler)dlsym(su->so_handle, UCE_COMPONENT_SYMBOL);
		dlerror();
		su->on_websocket = (request_ref_handler)dlsym(su->so_handle, UCE_WEBSOCKET_SYMBOL);
		dlerror();
		su->on_cli = (request_ref_handler)dlsym(su->so_handle, UCE_CLI_SYMBOL);
		dlerror();
		su->on_once = (request_ref_handler)dlsym(su->so_handle, UCE_ONCE_SYMBOL);
		dlerror();
		su->on_init = (request_ref_handler)dlsym(su->so_handle, UCE_INIT_SYMBOL);
		dlerror();
		su->api_declarations = split(file_get_contents(su->api_file_name), "\n");
		String init_error = "";
		if(!compiler_run_unit_init(context, su, &init_error))
		{
			if(init_error != "")
				su->compiler_messages = init_error;
			compiler_unload_failed_shared_unit(su);
		}
		//else
		//	printf("(i) loaded unit %s\n", su->file_name.c_str());
	}
	else
	{
		const char* dl_error = dlerror();
		su->compiler_messages = "could not open " + su->so_name;
		if(dl_error && String(dl_error) != "")
			su->compiler_messages += ": " + String(dl_error);
		su->compile_status = "load_error";
		su->compile_error_status = su->compiler_messages;
		su->last_error = time();
		printf("Error loading unit %s, %s\n", su->file_name.c_str(), su->compiler_messages.c_str());
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

void compile_shared_unit(Request* context, SharedUnit* su)
{
	f64 comp_start = time_precise();

	if(!file_exists(su->file_name))
	{
		su->compiler_messages = "source file not found (" + su->file_name + ")";
		file_put_contents(su->compile_output_file_name, su->compiler_messages + "\n");
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
		file_put_contents(su->compile_output_file_name, su->compiler_messages + "\n");
		compiler_record_compile_result(su, time_precise() - comp_start, false, "compile_error", su->compiler_messages);
		printf("%s \n", su->compiler_messages.c_str());
	}
	else
	{
		load_shared_unit(context, su);
		if(su->so_handle)
		{
			file_put_contents(su->meta_file_name, compiler_unit_metadata_text(context, su));
			file_unlink(su->compile_output_file_name);
		}
		else if(trim(su->compiler_messages) != "")
		{
			file_put_contents(su->compile_output_file_name, su->compiler_messages + "\n");
		}
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

	auto cached = compiler_reusable_cached_unit(context, file_name, opt_so_optional, force_recompile);
	if(cached)
		return(cached);

	compiler_release_cached_unit_if_needed(context, file_name, opt_so_optional, force_recompile);

	SharedUnit* su = new SharedUnit();
	setup_unit_paths(context, su, file_name);
	su->opt_so_optional = opt_so_optional;

	int fdlock = compiler_open_lock_file(su->so_name + ".lock", "shared-unit:" + file_name);
	if(fdlock == -1)
	{
		su->compiler_messages = "could not open compile lock";
		su->compile_status = "lock_error";
		su->compile_error_status = su->compiler_messages;
		su->last_error = time();
		context->server->units[file_name] = su;
		return(su);
	}

	cached = compiler_reusable_cached_unit(context, file_name, opt_so_optional, force_recompile);
	if(cached)
	{
		compiler_close_lock_file(fdlock);
		delete su;
		return(cached);
	}

	compiler_release_cached_unit_if_needed(context, file_name, opt_so_optional, force_recompile);

	auto state = inspect_shared_unit_filesystem(context, su);
	auto compile_check = shared_unit_compile_check(state);
	bool retry_deferred = compiler_failure_retry_deferred(context, su, state);
	bool jit_enabled = compiler_jit_compile_on_request_enabled(context);
	bool do_recompile = force_recompile || compile_check.needs_compile;
	if(do_recompile)
	{
		if(!force_recompile && retry_deferred)
			compiler_restore_persisted_failure(su, state);
		else if(!force_recompile && !jit_enabled)
		{
			compiler_restore_persisted_failure(su, state, "jit_compile_disabled");
			if(trim(su->compiler_messages) == "")
			{
				su->compiler_messages = "JIT compilation on request is disabled";
				su->compile_error_status = su->compiler_messages;
			}
		}
		else
			compile_shared_unit(context, su);
	}
	else
	{
		load_shared_unit(context, su);
		if(!su->so_handle)
		{
			if(!force_recompile && retry_deferred)
				compiler_restore_persisted_failure(su, state);
			else if(!force_recompile && !jit_enabled)
			{
				compiler_restore_persisted_failure(su, state, "jit_compile_disabled");
				if(trim(su->compiler_messages) == "")
				{
					su->compiler_messages = "JIT compilation on request is disabled";
					su->compile_error_status = su->compiler_messages;
				}
			}
			else
				compile_shared_unit(context, su);
		}
	}

	auto observed_state = inspect_shared_unit_filesystem(context, su);
	compiler_record_observed_filesystem_state(su, observed_state);

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
	return(compiler_with_registry_lock(context, [&]() {
		return(compiler_read_known_units_unlocked(context));
	}));
}

void compiler_set_known_units(Request* context, StringList files)
{
	files = compiler_normalize_unit_list(context, files);
	compiler_with_registry_lock(context, [&]() {
		compiler_write_known_units_unlocked(context, files);
		return(0);
	});
}

void compiler_track_known_unit(Request* context, String file_name)
{
	file_name = compiler_normalize_unit_path(context, file_name);
	if(file_name == "" || !compiler_is_known_unit_file(file_name))
		return;

	compiler_with_registry_lock(context, [&]() {
		auto files = compiler_read_known_units_unlocked(context);
		if(std::find(files.begin(), files.end(), file_name) != files.end())
			return(0);
		files.push_back(file_name);
		compiler_write_known_units_unlocked(context, files);
		return(0);
	});
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
}

bool compiler_unit_needs_recompile(Request* context, String file_name, bool* source_missing)
{
	file_name = compiler_normalize_unit_path(context, file_name);
	SharedUnit su;
	setup_unit_paths(context, &su, file_name);
	auto state = inspect_shared_unit_filesystem(context, &su);
	auto compile_check = shared_unit_compile_check(state);
	if(source_missing)
		*source_missing = compile_check.source_missing;
	if(compile_check.source_missing)
		return(false);
	if(compiler_failure_retry_deferred(context, &su, state))
		return(false);
	return(compile_check.needs_compile);
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
	if(su->compile_status == "unknown" && compiler_failure_retry_deferred(context, su, fs_state))
		compiler_restore_persisted_failure(su, fs_state);
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
	info["meta_file_name"] = su->meta_file_name;
	info["compile_output_file_name"] = su->compile_output_file_name;
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
	info["metadata_mtime"] = (f64)fs_state.metadata_time;
	info["compile_output_mtime"] = (f64)fs_state.compile_output_time;
	info["setup_template_mtime"] = (f64)fs_state.setup_template_time;
	info["required_mtime"] = (f64)fs_state.required_time;
	info["runtime_abi_version"] = (f64)fs_state.runtime_abi_version;
	info["metadata_abi_version"] = (f64)fs_state.metadata_abi_version;
	info["current_input_signature"] = fs_state.current_input_signature;
	info["metadata_input_signature"] = fs_state.metadata_input_signature;
	info["metadata_build_token"] = fs_state.metadata_build_token;
	compiler_tree_set_bool(info, "known", compiler_has_known_unit_cached(context, resolved_path));
	compiler_tree_set_bool(info, "current_unit", resolved_path == compiler_current_unit_path(context));
	compiler_tree_set_bool(info, "loaded", su->so_handle != 0);
	compiler_tree_set_bool(info, "source_exists", fs_state.source_exists);
	compiler_tree_set_bool(info, "compiled_exists", fs_state.compiled_time != 0);
	compiler_tree_set_bool(info, "metadata_exists", fs_state.metadata_exists);
	compiler_tree_set_bool(info, "metadata_parsed", fs_state.metadata_parsed);
	compiler_tree_set_bool(info, "abi_compatible", fs_state.abi_compatible);
	compiler_tree_set_bool(info, "input_signature_matches", fs_state.input_signature_matches);
	compiler_tree_set_bool(info, "stale", shared_unit_compile_check(fs_state).needs_compile && !compiler_failure_retry_deferred(context, su, fs_state));
	compiler_tree_set_bool(info, "retry_deferred", compiler_failure_retry_deferred(context, su, fs_state));
	compiler_tree_set_bool(info, "cache_stale", shared_unit_cache_is_stale(context, su));
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

enum class UnitCallMacroKind
{
	none,
	render,
	component,
	once,
	init,
	cli,
	serve_http
};

struct UnitCallMacroTarget
{
	UnitCallMacroKind kind = UnitCallMacroKind::none;
	String handler_name;
};

struct RequestPropsScope
{
	Request* context = 0;
	DTree previous_props;

	RequestPropsScope(Request* context, const DTree& props)
	{
		this->context = context;
		if(this->context)
		{
			previous_props = this->context->props;
			this->context->props = props;
		}
	}

	~RequestPropsScope()
	{
		if(context)
			context->props = previous_props;
	}
};

void compiler_unload_failed_shared_unit(SharedUnit* su)
{
	if(!su)
		return;
	if(su->so_handle)
		dlclose(su->so_handle);
	su->so_handle = 0;
	su->api_functions.clear();
	su->on_setup = 0;
	su->on_render = 0;
	su->on_component = 0;
	su->on_websocket = 0;
	su->on_cli = 0;
	su->on_once = 0;
	su->on_init = 0;
}

bool compiler_run_unit_init(Request* context, SharedUnit* su, String* error_out)
{
	if(!su || !su->on_init)
		return(true);
	if(!context)
	{
		if(error_out)
			*error_out = "internal error: INIT() requires a Request context";
		return(false);
	}
	if(!su->on_setup)
	{
		if(error_out)
			*error_out = "internal error: " + String(UCE_SETUP_SYMBOL) + "() not defined in " + su->file_name;
		return(false);
	}

	UnitInvocationScope invoke_scope(context, su);
	su->on_setup(context);
	try
	{
		su->on_init(*context);
		return(true);
	}
	catch(...)
	{
		su->runtime_error_status = "uncaught exception during INIT";
		su->compile_status = "load_error";
		su->compile_error_status = su->runtime_error_status;
		su->last_error = time();
		if(error_out)
			*error_out = su->runtime_error_status;
		return(false);
	}
}

bool compiler_run_unit_once_if_needed(Request* context, SharedUnit* su, String* error_out)
{
	if(!su || !su->on_once)
		return(true);
	if(!context)
	{
		if(error_out)
			*error_out = "internal error: ONCE() requires a Request context";
		return(false);
	}
	if(!su->on_setup)
	{
		if(error_out)
			*error_out = "internal error: " + String(UCE_SETUP_SYMBOL) + "() not defined in " + su->file_name;
		return(false);
	}

	if(context->once_units.find(su->file_name) != context->once_units.end())
		return(true);

	context->once_units.insert(su->file_name);
	UnitInvocationScope invoke_scope(context, su);
	su->on_setup(context);
	try
	{
		su->on_once(*context);
		return(true);
	}
	catch(...)
	{
		context->once_units.erase(su->file_name);
		su->runtime_error_status = "uncaught exception during ONCE";
		su->last_error = time();
		if(error_out)
			*error_out = su->runtime_error_status;
		throw;
	}
}

String unit_call_macro_trim(String function_name)
{
	function_name = trim(function_name);
	if(function_name.length() >= 2 && function_name.substr(function_name.length() - 2) == "()")
		function_name = trim(function_name.substr(0, function_name.length() - 2));
	return(function_name);
}

UnitCallMacroTarget unit_call_macro_target(String function_name)
{
	UnitCallMacroTarget target;
	function_name = unit_call_macro_trim(function_name);

	if(function_name == "RENDER")
	{
		target.kind = UnitCallMacroKind::render;
		return(target);
	}
	if(function_name.rfind("RENDER:", 0) == 0)
	{
		target.kind = UnitCallMacroKind::render;
		target.handler_name = trim(function_name.substr(7));
		return(target);
	}
	if(function_name == "COMPONENT")
	{
		target.kind = UnitCallMacroKind::component;
		return(target);
	}
	if(function_name.rfind("COMPONENT:", 0) == 0)
	{
		target.kind = UnitCallMacroKind::component;
		target.handler_name = trim(function_name.substr(10));
		return(target);
	}
	if(function_name == "ONCE")
	{
		target.kind = UnitCallMacroKind::once;
		return(target);
	}
	if(function_name == "INIT")
	{
		target.kind = UnitCallMacroKind::init;
		return(target);
	}
	if(function_name == "CLI")
	{
		target.kind = UnitCallMacroKind::cli;
		return(target);
	}
	if(function_name == "SERVE_HTTP")
	{
		target.kind = UnitCallMacroKind::serve_http;
		return(target);
	}
	if(function_name.rfind("SERVE_HTTP:", 0) == 0)
	{
		target.kind = UnitCallMacroKind::serve_http;
		target.handler_name = trim(function_name.substr(11));
		return(target);
	}

	return(target);
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
		return(UCE_RENDER_SYMBOL);
	return(String(UCE_RENDER_SYMBOL) + "_" + safe_name(render_name));
}

String component_handler_symbol(String render_name)
{
	render_name = trim(render_name);
	if(render_name == "")
		return(UCE_COMPONENT_SYMBOL);
	return(String(UCE_COMPONENT_SYMBOL) + "_" + safe_name(render_name));
}

String serve_http_handler_symbol(String handler_name)
{
	handler_name = trim(handler_name);
	if(handler_name == "")
		return(UCE_SERVE_HTTP_SYMBOL);
	return(String(UCE_SERVE_HTTP_SYMBOL) + "_" + safe_name(handler_name));
}

String compiler_missing_request_handler_message(UnitCallMacroKind kind, String handler_name)
{
	handler_name = trim(handler_name);
	if(kind == UnitCallMacroKind::render)
	{
		if(handler_name == "" || handler_name == "render")
			return("no RENDER() entry point");
		return("no RENDER:" + handler_name + "() entry point");
	}
	if(kind == UnitCallMacroKind::component)
	{
		if(handler_name == "")
			return("no COMPONENT() entry point");
		return("no COMPONENT:" + handler_name + "() entry point");
	}
	if(kind == UnitCallMacroKind::once)
		return("no ONCE() entry point");
	if(kind == UnitCallMacroKind::cli)
		return("no CLI() entry point");
	if(kind == UnitCallMacroKind::serve_http)
	{
		if(handler_name == "")
			return("no SERVE_HTTP() entry point");
		return("no SERVE_HTTP:" + handler_name + "() entry point");
	}
	if(kind == UnitCallMacroKind::init)
		return("no INIT() entry point");
	return("request handler not found");
}

bool compiler_prepare_request_handler(Request* context, SharedUnit* su, String* error_out = 0, bool run_once = false)
{
	if(!su->on_setup)
	{
		if(error_out)
			*error_out = "internal error: " + String(UCE_SETUP_SYMBOL) + "() not defined in " + su->file_name;
		return(false);
	}
	if(run_once && !compiler_run_unit_once_if_needed(context, su, error_out))
		return(false);
	return(true);
}

void compiler_execute_request_handler(
	Request* context,
	SharedUnit* su,
	request_ref_handler handler,
	bool count_request,
	String runtime_error_status
)
{
	UnitInvocationScope invoke_scope(context, su);
	su->on_setup(context);
	f64 render_start = time_precise();
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
			runtime_error_status
		);
		throw;
	}
}

request_ref_handler get_request_handler_symbol(SharedUnit* su, String symbol, request_ref_handler default_handler = 0)
{
	if(default_handler && symbol == "")
		return(default_handler);

	auto it = su->api_functions.find(symbol);
	if(it != su->api_functions.end())
		return((request_ref_handler)it->second);

	auto handler = (request_ref_handler)dlsym(su->so_handle, symbol.c_str());
	dlerror();
	su->api_functions[symbol] = (void*)handler;
	return(handler);
}

request_ref_handler get_page_render_handler(SharedUnit* su, String render_name)
{
	String symbol = page_render_handler_symbol(render_name);
	if(symbol == UCE_RENDER_SYMBOL)
		return(su->on_render);
	return(get_request_handler_symbol(su, symbol));
}

request_ref_handler get_component_handler(SharedUnit* su, String render_name)
{
	String symbol = component_handler_symbol(render_name);
	if(symbol == UCE_COMPONENT_SYMBOL)
		return(su->on_component);
	return(get_request_handler_symbol(su, symbol));
}

request_ref_handler get_serve_http_handler(SharedUnit* su, String handler_name)
{
	return(get_request_handler_symbol(su, serve_http_handler_symbol(handler_name)));
}

bool compiler_invoke_loaded_request_handler(
	Request* context,
	SharedUnit* su,
	request_ref_handler handler,
	UnitCallMacroKind kind,
	String handler_name,
	bool count_request,
	String runtime_error_status,
	String* error_out = 0
)
{
	if(!compiler_prepare_request_handler(context, su, error_out, true))
		return(false);

	if(!handler)
	{
		if(error_out)
			*error_out = compiler_missing_request_handler_message(kind, handler_name);
		return(false);
	}

	compiler_execute_request_handler(
		context,
		su,
		handler,
		count_request,
		runtime_error_status
	);
	return(true);
}

bool compiler_invoke_render(Request* context, String file_name, String render_name, String* error_out = 0)
{
	auto su = compiler_load_shared_unit(context, file_name, "", false);
	if(!su)
		return(false);

	return(compiler_invoke_loaded_request_handler(
		context,
		su,
		get_page_render_handler(su, render_name),
		UnitCallMacroKind::render,
		render_name,
		compiler_is_request_entry_unit(context, su),
		"uncaught exception during render",
		error_out
	));
}

bool compiler_invoke_component(Request* context, String file_name, String render_name, String* error_out = 0)
{
	auto su = compiler_load_shared_unit(context, file_name, "", false);
	if(!su)
		return(false);

	return(compiler_invoke_loaded_request_handler(
		context,
		su,
		get_component_handler(su, render_name),
		UnitCallMacroKind::component,
		render_name,
		false,
		"uncaught exception during component render",
		error_out
	));
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

void compiler_invoke_cli(Request* context, String file_name)
{
	auto su = compiler_load_shared_unit(context, file_name, "", false);
	if(!su)
		return;

	String error_message = "";
	if(!compiler_invoke_loaded_request_handler(
		context,
		su,
		su->on_cli,
		UnitCallMacroKind::cli,
		"",
		compiler_is_request_entry_unit(context, su),
		"uncaught exception during cli handler",
		&error_message
	))
	{
		if(!su->on_cli)
			context->set_status(404, "CLI Entry Point Not Found");
		else
			context->set_status(500, "CLI Unit Error");
		if(error_message != "")
			print(error_message);
	}
}

void compiler_invoke_serve_http(Request* context, String file_name, String handler_name)
{
	auto su = compiler_load_shared_unit(context, file_name, "", false);
	if(!su)
		return;

	String error_message = "";
	if(!compiler_invoke_loaded_request_handler(
		context,
		su,
		get_serve_http_handler(su, handler_name),
		UnitCallMacroKind::serve_http,
		handler_name,
		true,
		"uncaught exception during HTTP server handler",
		&error_message
	))
	{
		context->set_status(500, "HTTP Server Handler Error");
		if(error_message != "")
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
		printf("internal error: %s() not defined in %s\n", UCE_SETUP_SYMBOL, file_name.c_str());
		return;
	}

	if(!su->on_websocket)
	{
		printf("no WS() entry point in %s\n", file_name.c_str());
		return;
	}

	compiler_execute_request_handler(
		context,
		su,
		su->on_websocket,
		compiler_is_request_entry_unit(context, su),
		"uncaught exception during websocket handler"
	);
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

	RequestPropsScope props_scope(&context, props);

	String error_message = "";
	if(!compiler_invoke_component(&context, resolved_name, render_name, &error_message) && error_message != "")
		print(component_error_banner(error_message));
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
			print("internal error: ", UCE_SETUP_SYMBOL, "() not defined in ", file_name, "\n");
		}
		else
		{
			auto macro_target = unit_call_macro_target(function_name);
			if(macro_target.kind != UnitCallMacroKind::none)
			{
				RequestPropsScope props_scope(context, (call_param ? *call_param : DTree()));
				String error_message = "";

				if(macro_target.kind == UnitCallMacroKind::render)
				{
					if(!compiler_invoke_render(context, su->file_name, macro_target.handler_name, &error_message) && error_message != "")
						print("Error: unit_call() ", error_message);
				}
				else if(macro_target.kind == UnitCallMacroKind::component)
				{
					if(!compiler_invoke_component(context, su->file_name, macro_target.handler_name, &error_message) && error_message != "")
						print("Error: unit_call() ", error_message);
				}
				else
				{
					request_ref_handler handler = 0;

					if(macro_target.kind == UnitCallMacroKind::once)
						handler = su->on_once;
					else if(macro_target.kind == UnitCallMacroKind::init)
						handler = su->on_init;
					else if(macro_target.kind == UnitCallMacroKind::cli)
						handler = su->on_cli;

					if(!handler)
						print("Error: unit_call() ", compiler_missing_request_handler_message(macro_target.kind, macro_target.handler_name));
					else if(macro_target.kind == UnitCallMacroKind::cli)
					{
						String prepare_error = "";
						if(!compiler_prepare_request_handler(context, su, &prepare_error, true))
							print("Error: unit_call() ", prepare_error);
						else
							compiler_execute_request_handler(context, su, handler, false, "uncaught exception during cli handler");
					}
					else
					{
						UnitInvocationScope invoke_scope(context, su);
						su->on_setup(context);
						handler(*context);
					}
				}
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
	}
	else
	{
		print("Error: unit_call() could not load unit file '", file_name, "'");
	}
	return(result);
}
