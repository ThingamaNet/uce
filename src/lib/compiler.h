#pragma once

#define RENDER(X) extern "C" void __uce_render(Request& context)
#define COMPONENT(X) extern "C" void __uce_component(Request& context)
#define WS(X) extern "C" void __uce_websocket(Request& context)
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
String compiler_site_directory(Request* context);
StringList compiler_scan_site_units(Request* context);
StringList compiler_list_known_units(Request* context);
void compiler_set_known_units(Request* context, StringList files);
void compiler_track_known_unit(Request* context, String file_name);
void compiler_untrack_known_unit(Request* context, String file_name);
bool compiler_unit_needs_recompile(Request* context, String file_name, bool* source_missing = 0);
DTree unit_info(String path = "");
StringList units_list();
bool unit_compile(String path = "");

SharedUnit* unit_load(String file_name);
void unit_render(String file_name);
void unit_render(String file_name, Request& context);
DTree* unit_call(String file_name, String function_name, DTree* call_param = 0);
String component_resolve(String name);
bool component_exists(String name);
void component_render(String name);
void component_render(String name, Request& context);
void component_render(String name, DTree props);
void component_render(String name, DTree props, Request& context);
String component(String name);
String component(String name, Request& context);
String component(String name, DTree props);
String component(String name, DTree props, Request& context);
