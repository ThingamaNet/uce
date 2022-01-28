#include "functionlib.h"

String var_dump(StringMap map, String prefix, String postfix)
{
	String result = "";

	for (auto it = map.begin(); it != map.end(); ++it)
	{
		result.append(prefix + it->first + ": " + it->second + postfix);
	}

	return(result);
}

String var_dump(StringList slist, String prefix, String postfix)
{
	String result = "";

	for (auto& s : slist)
	{
		result.append(prefix + s + postfix);
	}

	return(result);
}

u8 char_to_u8(char input)
{
	if(input >= '0' && input <= '9')
		return input - '0';
	if(input >= 'A' && input <= 'F')
		return input - 'A' + 10;
	if(input >= 'a' && input <= 'f')
		return input - 'a' + 10;
	return(0);
}

u8 hex_to_u8(String src)
{
	return(char_to_u8(src[0])*16 + char_to_u8(src[1]));
}

String to_lower(String s)
{
	String result = "";
	for(auto c : s)
	{
		if(c >= 'A' && c <= 'Z')
			c = tolower(c);
		result.append(1, c);
	}
	return(result);
}

String to_upper(String s)
{
	String result = "";
	for(auto c : s)
	{
		if(c >= 'A' && c <= 'Z')
			c = toupper(c);
		result.append(1, c);
	}
	return(result);
}

String replace(String s, String search, String replace_with)
{
	s64 last_spos = 0;
	auto spos = s.find(search);
	if(spos == std::string::npos)
		return(s);
	String result = "";
	auto slen = search.length();
	while(spos != std::string::npos)
	{
		if(spos - last_spos > 0)
			result.append(s.substr(last_spos, spos - last_spos));
		result.append(replace_with);
		last_spos = spos + slen;
		spos = s.find(search, last_spos);
	}
	if(last_spos < s.length())
		result.append(s.substr(last_spos));
	return(result);
}

String trim(String raw)
{
	u32 len = raw.length();
	u32 start_pos = 0;
	u32 end_pos = len - 1;
	if(len == 0 || (len == 1 && isspace(raw[0])))
		return("");
	while(start_pos < len && isspace(raw[start_pos]))
		start_pos++;
	while(end_pos >= 0 && isspace(raw[end_pos]))
		end_pos--;
	if(end_pos < start_pos)
		return("");
	return(raw.substr(start_pos, 1 + end_pos - start_pos));
}

StringList split(String str)
{
	StringList result;
	String current_token = "";
	for(auto c : str)
	{
		if(isspace(c))
		{
			if(current_token != "")
			{
				result.push_back(current_token);
				current_token = "";
			}
		}
		else
		{
			current_token.append(1, c);
		}
	}
	if(current_token != "")
		result.push_back(current_token);
	return(result);
}

StringList split(String str, String delim)
{
	StringList result;
	int start = 0;
    int end = str.find(delim);
    while (end != String::npos)
    {
		result.push_back(str.substr(start, end - start));
        start = end + delim.size();
        end = str.find(delim, start);
    }
    result.push_back(str.substr(start, end - start));
    return(result);
}

String join(StringList l, String delim)
{
	String result;
	u32 i = 0;
	for(auto& s : l)
	{
		if(i > 0)
			result.append(delim);
		result.append(s);
		i += 1;
	}
	return(result);
}

StringList split_utf8(String s)
{
	StringList result;
	auto len = s.size();
	String codepoint = "";
	for(s64 i = 0; i < len; i++)
	{
		u8 c = s[i];
		if(is_bit_set(c, 7))
		{
			codepoint = "";
			codepoint.append(1, c);
			if(is_bit_set(c, 6))
			{
				codepoint.append(1, s[++i]);
				if(is_bit_set(c, 5))
				{
					codepoint.append(1, s[++i]);
					if(is_bit_set(c, 4))
					{
						codepoint.append(1, s[++i]);
					}
				}
			}
			result.push_back(codepoint);
		}
		else
		{
			result.push_back(String().append(1, c));
		}
	}
	return(result);
}

String html_escape(String s)
{
	String result;

	for(u32 i = 0; i < s.length(); i++)
	{
		char c = s[i];
		switch(c)
		{
			case('&'):
				result.append("&amp;");
				break;
			case('<'):
				result.append("&lt;");
				break;
			case('>'):
				result.append("&gt;");
				break;
			case('"'):
				result.append("&quot;");
				break;
			default:
				result.append(1, c);
				break;
		}
	}

	return(result);
}

String html_escape(u64 a)
{
	return(std::to_string(a));
}

String html_escape(f64 a)
{
	return(std::to_string(a));
}

u64 int_val(String s, u32 base)
{
	return(strtoll(s.c_str(), 0, base));
}

String nibble(String& haystack, String delim)
{
	auto idx = haystack.find(delim);
	if(idx == String::npos)
	{
		String result = haystack;
		haystack = "";
		return(result);
	}
	else
	{
		String result = haystack.substr(0, idx);
		haystack = haystack.substr(idx+delim.length());
		return(result);
	}
}

String json_encode(DTree t)
{
	String result = "";
	if(t.is_array())
	{
		result += "{";
		u32 count = 0;
		t.each([&] (DTree item, String key) {
			if(count > 0)
				result += ", ";
			count += 1;
			result += json_escape(key) + ": " + json_encode(item);
		});
		result += "}";
	}
	else
	{
		result = t.to_json();
	}
	return(result);
}

// https://i.stack.imgur.com/SHLOB.gif
String json_decode_String(String s, u32& i, char termination_char)
{
	String result;
	//print("json_decode_String " + s.substr(i) + "\n");
	while(i < s.length())
	{
		char c = s[i];
		if(c == termination_char)
		{
			i += 1;
			//print("json_decode_String = " + result + "\n");
			return(result);
		}
		else if(c == '\\')
		{
			i += 1;
			c = s[i];
			switch(c)
			{
				case('t'):
					result.append(1, '\t');
					break;
				case('n'):
					result.append(1, '\n');
					break;
				case('r'):
					result.append(1, '\r');
					break;
				case('\\'):
					result.append(1, '\\');
					break;
				case('b'):
					result.append(1, '\b');
					break;
				case('f'):
					result.append(1, '\f');
					break;
				case('u'):
					// todo decode
					break;
				default:
					result.append(1, c);
					break;
			}
		}
		else
		{
			result.append(1, c);
		}
		i += 1;
	}
	return(result);
}

DTree json_decode_map(String s, u32& i);

void json_consume_space(String s, u32& i)
{
	while(i < s.length() && isspace(s[i]))
		i += 1;
}

String json_decode_keyword(String s, u32& i)
{
	String result;
	json_consume_space(s, i);
	while(i < s.length())
	{
		char c = s[i];
		if(isalnum(c))
		{
			result.append(1, c);
		}
		else
		{
			return(result);
		}
		i += 1;
	}
	return(result);
}

String json_decode_number(String s, u32& i)
{
	String result;
	json_consume_space(s, i);
	while(i < s.length())
	{
		char c = s[i];
		if(isdigit(c) || c == '.')
		{
			result.append(1, c);
		}
		else
		{
			return(result);
		}
		i += 1;
	}
	return(result);
}

DTree json_decode_value(String s, u32& i)
{
	DTree result;
	String value = "";
	json_consume_space(s, i);
	char c = s[i];
	//print("json_decode_value " + s.substr(i) + "\n");
	if(c == '"' || c == '\'') // String value
	{
		result.type = 'S';
		i += 1;
		result._String = json_decode_String(s, i, s[i-1]);
		return(result);
	}
	else if(isdigit(c))
	{
		result.type = 'S';
		result._String = json_decode_number(s, i);
		//result._float = stod(json_decode_number(s, i));
		return(result);
	}
	else if(c == '{')
	{
		i += 1;
		return(json_decode_map(s, i));
	}
	else
	{
		value = json_decode_keyword(s, i);
		if(value == "true")
			result.set_bool(true);
		else if(value == "false")
			result.set_bool(false);
		else if(value == "null")
			result.set("");
		return(result);
	}
	return(result);
}

DTree json_decode_map(String s, u32& i)
{
	DTree result;
	result.type = 'M';
	String key = "";
	json_consume_space(s, i);
	//print("json_decode_map " + s.substr(i) + "\n");
	while(i < s.length())
	{
		char c = s[i];
		if(c == '}')
		{
			i += 1;
			return(result);
		}
		else if(c == ',')
		{
			i += 1;
		}
		else if(c == '"' || c == '\'')
		{
			i += 1;
			key = json_decode_String(s, i, s[i-1]);
			json_consume_space(s, i);
			if(s[i] != ':')
				return(result); // malformed
			i += 1;
			DTree v = json_decode_value(s, i);
			//result._map[key] = json_decode_value(s, i);
			//print("KV " + key + " = " + to_String(v) + "\n");
			//printf("map add %s (%c) \n", key.c_str(), s[i]);
			result._map[key] = v;
		}
		else
		{
			// malformed
			return(result);
		}
		json_consume_space(s, i);
	}
	return(result);
}

DTree json_decode(String s)
{
	u32 i = 0;
	return(json_decode_value(s, i));
}

void ob_start()
{
	context->ob_start();
}

void ob_close()
{
	delete context->ob;
	context->ob_stack.pop_back();
	if(context->ob_stack.size() == 0)
		ob_start();
}

String ob_get()
{
	return(context->ob->str());
}

String ob_get_close()
{
	String result = context->ob->str();
	ob_close();
	return(result);
}

