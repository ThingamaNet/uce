#pragma once

#define RENDER(X) extern "C" void render(Request& context)
#define COMPONENT(X) extern "C" void component_render(Request& context)
#define WS(X) extern "C" void websocket(Request& context)
#define EXPORT extern "C"

String process_html_literal(Request* context, SharedUnit* su, String content);
String preprocess_shared_unit(Request* context, SharedUnit* su);
void setup_unit_paths(Request* context, SharedUnit* su, String file_name);
void load_shared_unit(Request* context, SharedUnit* su, String file_name);
void compile_shared_unit(Request* context, SharedUnit* su, String file_name);
SharedUnit* get_shared_unit(Request* context, String file_name, bool opt_so_optional = false);
void compiler_invoke(Request* context, String file_name);
void compiler_invoke_websocket(Request* context, String file_name);
SharedUnit* compiler_load_shared_unit(Request* context, String file_name, String current_path = "", bool opt_so_optional = false);

SharedUnit* load_file(String file_name);
void render_file(String file_name);
void render_file(String file_name, Request& context);
DTree* call_file(String file_name, String function_name, DTree* call_param = 0);
String component_resolve(String name);
bool component_exists(String name);
void render_component(String name);
void render_component(String name, Request& context);
void render_component(String name, DTree props);
void render_component(String name, DTree props, Request& context);
String component(String name);
String component(String name, Request& context);
String component(String name, DTree props);
String component(String name, DTree props, Request& context);

StringList precompile_jobs;
