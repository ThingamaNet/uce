#define RENDER() extern "C" void render(DTree& call)

String process_html_literal(Request* context, SharedUnit* su, String content);
String preprocess_shared_unit(Request* context, SharedUnit* su);
void setup_unit_paths(Request* context, SharedUnit* su, String file_name);
void load_shared_unit(Request* context, SharedUnit* su, String file_name);
void compile_shared_unit(Request* context, SharedUnit* su, String file_name);
SharedUnit* get_shared_unit(Request* context, String file_name);
void compiler_invoke(Request* context, String file_name, DTree& call_param);

