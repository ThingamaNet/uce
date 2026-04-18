#include "uri.h"

String var_dump(URI uri, String prefix, String postfix)
{
	return(
		prefix + "URI Parts: " + postfix +
		var_dump(uri.parts, prefix+"  ", postfix)+
		prefix + "  Query: " + postfix +
		var_dump(uri.query, prefix+"    ", postfix)
	);
}

String uri_decode(String q)
{
	String result;
	for(u32 i = 0; i < q.length(); i++)
	{
		char c = q[i];
		if(c == '%' && q[i+1] != '%')
		{
			result.append(1, hex_to_u8(q.substr(i+1, 2)));
			i += 2;
		}
		else if(c == '+')
		{
			result.append(1, ' ');
		}
		else
		{
			result.append(1, c);
		}
	}
	return(result);
}

String uri_encode(String q)
{
	String result;
	for(u32 i = 0; i < q.length(); i++)
	{
		char c = q[i];
		if(isalnum(c) || c == '~' || c == '.' || c == '_' || c == '-')
			result.append(1, c);
		else
		{
			result.append(1, '%');
			result.append(to_hex(c));
		}
	}
	return(result);
}

StringMap parse_query(String q)
{
	StringMap result;
	if(q.length() == 0)
		return(result);

	bool is_key = true;
	String key = "";
	String value = "";
	for (char &c: q)
	{
		if(c == '=')
		{
			is_key = !is_key;
		}
		else if(c == '&')
		{
			result[uri_decode(key)] = uri_decode(value);
			key = "";
			value = "";
			is_key = true;
		}
		else if(is_key)
		{
			key.append(1, c);
		}
		else
		{
			value.append(1, c);
		}
	}

	result[uri_decode(key)] = uri_decode(value);

	return(result);
}

String encode_query(StringMap map)
{
	String result;

	for (auto it = map.begin(); it != map.end(); ++it)
	{
		if(result.length() > 0)
			result.append(1, '&');
		result.append(uri_encode(it->first) + "=" + uri_encode(it->second));
	}

	return(result);
}

String trim_wrapping_quotes(String raw)
{
	if(raw.length() >= 2 && raw[0] == '"' && raw[raw.length()-1] == '"')
		return(raw.substr(1, raw.length()-2));
	return(raw);
}

void parse_multipart_content_disposition(
	String raw,
	String& disposition_type,
	String& field_name,
	String& file_name)
{
	auto parts = split(raw, ";");
	if(parts.size() == 0)
		return;

	disposition_type = to_lower(trim(parts[0]));
	for(u32 i = 1; i < parts.size(); i++)
	{
		String part = trim(parts[i]);
		String key = to_lower(trim(nibble(part, "=")));
		String value = trim_wrapping_quotes(trim(part));
		if(key == "name")
			field_name = value;
		else if(key == "filename")
			file_name = value;
	}
}

String make_upload_tmp_name()
{
	String upload_path = context->server->config["TMP_UPLOAD_PATH"];
	if(upload_path.length() > 0 && upload_path[upload_path.length()-1] != '/')
		upload_path.append(1, '/');
	return(upload_path + make_session_id());
}

StringMap parse_multipart(String q, String boundary, std::vector<UploadedFile>& uploaded_files)
{
	StringMap result;

	if(boundary == "")
		return(result);

	u64 i = 0;
	while(i < q.length())
	{
		auto start_pos = q.find(boundary, i);
		if(start_pos == String::npos)
			break;

		start_pos += boundary.length();
		if(q.substr(start_pos, 2) == "--")
			break;
		if(q.substr(start_pos, 2) == "\r\n")
			start_pos += 2;

		auto header_end = q.find("\r\n\r\n", start_pos);
		if(header_end == String::npos)
			break;

		String header_block = q.substr(start_pos, header_end - start_pos);
		auto end_pos = q.find(boundary, header_end + 4);
		if(end_pos == String::npos)
			break;

		String body = q.substr(header_end + 4, end_pos - (header_end + 4));
		if(body.length() >= 2 && body.substr(body.length()-2) == "\r\n")
			body.resize(body.length()-2);

		String disposition_type;
		String field_name;
		String file_name;
		for(auto header_line : split(header_block, "\r\n"))
		{
			String header_name = to_lower(trim(nibble(header_line, ":")));
			String header_value = trim(header_line);
			if(header_name == "content-disposition")
			{
				parse_multipart_content_disposition(
					header_value,
					disposition_type,
					field_name,
					file_name
				);
			}
		}

		if(field_name != "")
		{
			bool is_uploaded_file =
				(disposition_type == "form-data" || disposition_type == "attachment") &&
				file_name != "";

			if(is_uploaded_file)
			{
				UploadedFile f;
				f.tmp_name = make_upload_tmp_name();
				f.file_name = file_name;
				f.size = body.length();
				file_put_contents(f.tmp_name, body);
				uploaded_files.push_back(f);
				result[field_name] = file_name;
			}
			else
			{
				result[field_name] = body;
			}
		}

		i = end_pos + boundary.length();
	}

	return(result);
}

URI parse_uri(String uri_String)
{
	URI result;

	u8 state = 0;
	String current = "";
	char expect = 0;

	result.parts["raw"] = uri_String;

	String part_names[] = {
		"scheme",
		"host",
		"port",
		"path",
		"query",
		"fragment",
	};

	if(uri_String[0] == '/')
		state = 3;

	for (char &c: uri_String)
	{
		bool append_it = true;

		if(expect && expect != c)
		{
			result.parts["error"] = String("\'") + c + String("\' expected");
			result.parts["error_parsing"] = current;
			result.parts["error_part"] = part_names[state];
			return(result);
		}
		expect = 0;

		switch(state)
		{
			case(0): // scheme
				if(c == ':')
				{
					result.parts[part_names[state]] = current;
					append_it = false;
					current = "";
					state = 1;
				}
				break;
			case(1): // host name
				if(c == '/')
				{
					if(current == "")
					{
						append_it = false;
						break;
					}
					result.parts[part_names[state]] = current;
					append_it = false;
					current = "";
					state = 3;
					expect = '/';
				}
				else if(c == ':')
				{
					result.parts[part_names[state]] = current;
					append_it = false;
					current = "";
					state = 2;
				}
				break;
			case(2): // port
				if(c == '/')
				{
					result.parts[part_names[state]] = current;
					append_it = false;
					current = "";
					state = 3;
					expect = '/';
				}
				break;
			case(3): // path
				if(c == '/' && current == "")
				{
					append_it = false;
					break;
				}
				if(c == '?')
				{
					result.parts[part_names[state]] = current;
					append_it = false;
					current = "";
					state = 4;
				}
				break;
			case(4): // query
				if(c == '#')
				{
					result.parts[part_names[state]] = current;
					append_it = false;
					current = "";
					state = 5;
				}
				break;
		}

		if(append_it)
			current.append(1, c);
	}

	result.parts[part_names[state]] = current;

	result.query = parse_query(result.parts["query"]);

	return(result);
}

void set_cookie(
	String name, String value,
	u64 expires, String path, String domain,
	bool secure, bool http_only)
{
	String cookie = "Set-Cookie: ";
	cookie.append(uri_encode(name) + "=" + uri_encode(value));
	if(expires > 0)
		cookie.append(String("; Expires=") + gmdate("RFC1123", expires));
	context->set_cookies.push_back(cookie);
	context->cookies[name] = value;
}

StringMap parse_cookies(String cookie_String)
{
	StringMap result;
	while(cookie_String.length() > 0)
	{
		String key = trim(nibble("=", cookie_String));
		String value = nibble(";", cookie_String);
		result[key] = value;
	}
	return(result);
}

String make_session_id()
{
	return(to_hex(rand())+to_hex(rand())+to_hex(rand())+to_hex(rand()));
}

bool is_valid_session_id(String session_id)
{
	if(session_id.length() < 16 || session_id.length() > 128)
		return(false);
	for(auto c : session_id)
	{
		if(!isxdigit(c))
			return(false);
	}
	return(true);
}

String session_file_path(String session_id)
{
	if(!is_valid_session_id(session_id))
		return("");
	return(context->server->config["SESSION_PATH"] + "/" + session_id);
}

StringMap load_session_data(String session_id)
{
	String session_path = session_file_path(session_id);
	if(session_path == "")
		return(StringMap());
	return(parse_query(file_get_contents(session_path)));
}

void save_session_data(String session_id, StringMap data)
{
	String session_path = session_file_path(session_id);
	if(session_path == "")
	{
		printf("(!) Refusing to save invalid session id\n");
		return;
	}
	file_put_contents(session_path, encode_query(data));
}

String session_start(String session_name)
{
	String session_id = context->cookies[session_name];
	if(!is_valid_session_id(session_id))
		session_id = "";

	if(session_id.length() == 0)
	{
		session_id = make_session_id();
		set_cookie(session_name, session_id, time() + int_val(context->server->config["SESSION_TIME"]));
	}
	context->session_id = session_id;
	context->session_name = session_name;
	context->session = load_session_data(context->session_id);
	return(context->session_id);
}

void session_destroy(String session_name)
{
	if(context->cookies[session_name].length() > 0)
	{
		set_cookie(session_name, "", time() - int_val(context->server->config["SESSION_TIME"]));
		context->session.clear();
		save_session_data(context->session_id, context->session);
		context->session_id = "";
	}
}
