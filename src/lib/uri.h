

String var_dump(URI uri, String prefix = "", String postfix = "\n");
String uri_decode(String q);
String uri_encode(String q);
StringMap parse_query(String q);
String encode_query(StringMap map);
StringMap parse_multipart(String q, String boundary, std::vector<UploadedFile>& uploaded_files);
URI parse_uri(String uri_String);
void set_cookie(
	String name, String value = "",
	u64 expires = 0, String path = "/", String domain = "",
	bool secure = false, bool http_only = true);
StringMap parse_cookies(String cookie_String);
String make_session_id();
StringMap load_session_data(String session_id);
void save_session_data(String session_id, StringMap data);
String session_start(String session_name = "uce-session");
void session_destroy(String session_name = "uce-session");
