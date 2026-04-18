

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

struct WSFrame {

	u8 opcode;
	u8 is_final_fragment;
	u8 mask_bit;
	u64 payload_length;
	u64 offset;
	u8* payload;

	WSFrame(String& bufstr)
	{
		const char* buffer = bufstr.c_str();
		u64 buffer_size = bufstr.size();
		opcode = buffer[0] & 0x0F;
		is_final_fragment = buffer[0] & 0x80;
		mask_bit = buffer[1] & 0x80;
		payload_length = buffer[1] & 0x7F;

		offset = 2;
		if (payload_length == 126) {
			payload_length = ((u64) buffer[2] << 8) | buffer[3];
			offset = 4;
		} else if (payload_length == 127) {
			payload_length = ((u64) buffer[2] << 56) |
				((u64) buffer[3] << 48) |
				((u64) buffer[4] << 40) |
				((u64) buffer[5] << 32) |
				((u64) buffer[6] << 24) |
				((u64) buffer[7] << 16) |
				((u64) buffer[8] << 8) |
				buffer[9];
			offset = 10;
		}

		uint8_t* maskingKey = nullptr;
		if (mask_bit) {
			maskingKey = reinterpret_cast<uint8_t*>(const_cast<char*>(buffer + offset));
			offset += 4;
		}

		// Parse the frame payload
		payload = reinterpret_cast<uint8_t*>(const_cast<char*>(buffer + offset));
		if (mask_bit) {
			for (int i = 0; i < payload_length; i++) {
				payload[i] = payload[i] ^ maskingKey[i % 4];
			}
		}

		// Print the parsed frame
		std::cout << "Parsed WebSocket frame:" << std::endl;
		std::cout << "  Opcode: " << (int) opcode << std::endl;
		std::cout << "  Is final fragment: " << (is_final_fragment ? "true" : "false") << std::endl;
		std::cout << "  Payload length: " << payload_length << std::endl;
		if (mask_bit) {
			std::cout << "  Masking key: ";
			for (int i = 0; i < 4; i++) {
				std::cout << std::hex << (int) maskingKey[i] << " ";
			}
			std::cout << std::endl;
		}
		std::cout << "  Payload: " << std::endl << std::string((const char*) payload, payload_length) << std::endl;

	}

};
