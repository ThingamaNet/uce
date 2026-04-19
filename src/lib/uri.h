
#pragma once


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
String ws_make_accept_key(String client_key);
bool ws_is_valid_client_key(String client_key);
String ws_encode_frame(String payload, u8 opcode = 0x1, bool is_final_fragment = true);
String ws_close_frame(u16 status_code = 1000, String reason = "");
bool ws_is_valid_utf8(String input);

struct WSFrame {

	u8 opcode = 0;
	bool is_final_fragment = false;
	bool mask_bit = false;
	bool rsv1 = false;
	bool rsv2 = false;
	bool rsv3 = false;
	u64 payload_length = 0;
	u64 header_length = 0;
	u64 frame_length = 0;
	String payload;

	bool parse(const String& buffer, String& error);

};
