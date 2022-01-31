#define RENDER() extern "C" void render(DTree& call)
#define EXPORT extern "C"

String process_html_literal(Request* context, SharedUnit* su, String content);
String preprocess_shared_unit(Request* context, SharedUnit* su);
void setup_unit_paths(Request* context, SharedUnit* su, String file_name);
void load_shared_unit(Request* context, SharedUnit* su, String file_name);
void compile_shared_unit(Request* context, SharedUnit* su, String file_name);
SharedUnit* get_shared_unit(Request* context, String file_name, bool opt_so_optional = false);
void compiler_invoke(Request* context, String file_name, DTree& call_param);
SharedUnit* compiler_load_shared_unit(Request* context, String file_name, String current_path, bool opt_so_optional = false);

void render_file(String file_name);
void render_file(String file_name, DTree& call_param);
DTree* call_file_function(String file_name, String function_name, DTree* call_param = 0);

StringList precompile_jobs;
