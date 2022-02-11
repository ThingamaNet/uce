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

StringMap parse_multipart(String q, String boundary, std::vector<UploadedFile>& uploaded_files)
{
	StringMap result;

	auto i = boundary.length();
	while(i < q.length())
	{
		auto end_pos = q.find(boundary, i);
		if(end_pos != String::npos)
		{
			String field = q.substr(i, end_pos - i);
			nibble(":", field);
			String ftype = trim(nibble(";", field));
			if(ftype == "form-data")
			{
				nibble("=\"", field);
				String field_name = nibble("\"", field);
				result[field_name] = field.substr(4, field.length()-6);
			}
			else if(ftype == "attachment")
			{
				nibble("=\"", field);
				UploadedFile f;
				f.tmp_name = context->server->config["TMP_UPLOAD_PATH"] + std::to_string(rand());
				f.file_name = nibble("\"", field);
				String bin = field.substr(4, field.length()-6);
				f.size = bin.length();
				file_put_contents(f.tmp_name, bin);
				uploaded_files.push_back(f);
			}
		}
		else
		{
			// we're done
			end_pos = q.length();
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

StringMap load_session_data(String session_id)
{
	return(parse_query(file_get_contents(context->server->config["SESSION_PATH"] + "/" + session_id)));
}

void save_session_data(String session_id, StringMap data)
{
	file_put_contents(context->server->config["SESSION_PATH"] + "/" + session_id, encode_query(data));
}

String session_start(String session_name)
{
	if(context->cookies[session_name].length() == 0)
	{
		set_cookie(session_name, make_session_id(), time() + int_val(context->server->config["SESSION_TIME"]));
	}
	context->session_id = context->cookies[session_name];
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
