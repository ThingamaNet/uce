#include "functionlib.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <cctype>
#include <stdexcept>
#include <algorithm>

String var_dump(StringMap map, String prefix, String postfix)
{
	String result = "";

	for (auto it = map.begin(); it != map.end(); ++it)
	{
		result.append(prefix + to_upper(it->first) + ": " + it->second + postfix);
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
	String result = s;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return((char)std::tolower(c)); });
	return(result);
}

String to_upper(String s)
{
	String result = s;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return((char)std::toupper(c)); });
	return(result);
}

StringList list_unique(StringList items)
{
	StringList result;
	std::set<String> seen;
	for(auto item : items)
	{
		if(seen.find(item) != seen.end())
			continue;
		seen.insert(item);
		result.push_back(item);
	}
	return(result);
}

StringList list_sort(StringList items)
{
	std::sort(items.begin(), items.end());
	return(items);
}

bool list_some(StringList items, std::function<bool (String)> f)
{
	for(auto item : items)
	{
		if(f(item))
			return(true);
	}
	return(false);
}

bool list_every(StringList items, std::function<bool (String)> f)
{
	for(auto item : items)
	{
		if(!f(item))
			return(false);
	}
	return(true);
}

String list_find(StringList items, std::function<bool (String)> f, String fallback)
{
	for(auto item : items)
	{
		if(f(item))
			return(item);
	}
	return(fallback);
}

StringList dtree_keys(DTree tree)
{
	StringList result;
	tree.each([&](const DTree& item, String key) {
		if(key != "")
			result.push_back(key);
	});
	return(result);
}

DTree dtree_values(DTree tree)
{
	DTree result;
	result.set_array();
	tree.each([&](const DTree& item, String key) {
		result.push(item);
	});
	return(result);
}

DTree dtree_pick(DTree tree, StringList keys)
{
	DTree result;
	for(auto key : keys)
	{
		DTree* item = tree.key(key);
		if(item)
			result[key] = *item;
	}
	return(result);
}

DTree dtree_omit(DTree tree, StringList keys)
{
	DTree result;
	std::set<String> omitted(keys.begin(), keys.end());
	tree.each([&](const DTree& item, String key) {
		if(key != "" && omitted.find(key) == omitted.end())
			result[key] = item;
	});
	return(result);
}

DTree dtree_map(DTree tree, std::function<DTree (const DTree&, String)> f)
{
	DTree result;
	bool input_is_list = tree.is_list();
	if(input_is_list)
		result.set_array();
	tree.each([&](const DTree& item, String key) {
		DTree mapped = f(item, key);
		if(key != "" && !input_is_list)
			result[key] = mapped;
		else
			result.push(mapped);
	});
	return(result);
}

DTree dtree_filter(DTree tree, std::function<bool (const DTree&, String)> f)
{
	DTree result;
	bool input_is_list = tree.is_list();
	if(input_is_list)
		result.set_array();
	tree.each([&](const DTree& item, String key) {
		if(!f(item, key))
			return;
		if(key != "" && !input_is_list)
			result[key] = item;
		else
			result.push(item);
	});
	return(result);
}

DTree dtree_group_by(DTree tree, std::function<String (const DTree&, String)> f)
{
	DTree result;
	tree.each([&](const DTree& item, String key) {
		String group = f(item, key);
		DTree* group_items = result.get_or_create(group);
		if(!group_items->is_array())
			group_items->set_array();
		group_items->push(item);
	});
	return(result);
}

String substr(String s, s64 start_pos)
{
	s64 len = s.length();
	s64 start = start_pos;
	if(start < 0)
		start = len + start;
	if(start < 0)
		start = 0;
	if(start >= len)
		return("");
	return(s.substr(start));
}

String substr(String s, s64 start_pos, s64 length)
{
	s64 len = s.length();
	s64 start = start_pos;
	if(start < 0)
		start = len + start;
	if(start < 0)
		start = 0;
	if(start >= len)
		return("");

	s64 end = len;
	if(length >= 0)
		end = start + length;
	else
		end = len + length;

	if(end < start)
		return("");
	if(end > len)
		end = len;
	return(s.substr(start, end - start));
}

s64 strpos(String haystack, String needle, s64 offset)
{
	s64 start = offset;
	s64 len = haystack.length();
	if(start < 0)
		start = len + start;
	if(start < 0)
		start = 0;
	if(start > len)
		return(-1);
	if(needle == "")
		return(start);
	auto pos = haystack.find(needle, start);
	if(pos == std::string::npos)
		return(-1);
	return(pos);
}

bool str_starts_with(String haystack, String needle)
{
	if(needle.length() > haystack.length())
		return(false);
	return(haystack.compare(0, needle.length(), needle) == 0);
}

bool str_ends_with(String haystack, String needle)
{
	if(needle.length() > haystack.length())
		return(false);
	return(haystack.compare(haystack.length() - needle.length(), needle.length(), needle) == 0);
}

bool contains(String haystack, String needle)
{
	if(needle == "")
		return(true);
	return(haystack.find(needle) != std::string::npos);
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

namespace {

String regex_flags_label(String flags)
{
	return(flags == "" ? "default" : flags);
}

void regex_throw(String function_name, String message)
{
	throw std::runtime_error(function_name + "(): " + message);
}

String regex_pcre2_error(int error_code)
{
	PCRE2_UCHAR buffer[256];
	pcre2_get_error_message(error_code, buffer, sizeof(buffer));
	return(String(reinterpret_cast<char*>(buffer)));
}

uint32_t regex_compile_options(String flags, String function_name)
{
	uint32_t options = PCRE2_UTF | PCRE2_UCP;
	for(char flag : flags)
	{
		switch(flag)
		{
			case('i'):
				options |= PCRE2_CASELESS;
				break;
			case('m'):
				options |= PCRE2_MULTILINE;
				break;
			case('s'):
				options |= PCRE2_DOTALL;
				break;
			case('x'):
				options |= PCRE2_EXTENDED;
				break;
			case('u'):
				options |= PCRE2_UTF | PCRE2_UCP;
				break;
			case('a'):
				options &= ~PCRE2_UTF;
				options &= ~PCRE2_UCP;
				break;
			default:
				regex_throw(function_name, "unknown regex flag '" + String(1, flag) + "'");
		}
	}
	return(options);
}

struct RegexCode {
	pcre2_code* code = 0;

	RegexCode(String pattern, String flags, String function_name)
	{
		int error_code = 0;
		PCRE2_SIZE error_offset = 0;
		code = pcre2_compile(
			reinterpret_cast<PCRE2_SPTR>(pattern.c_str()),
			pattern.length(),
			regex_compile_options(flags, function_name),
			&error_code,
			&error_offset,
			0
		);
		if(!code)
			regex_throw(function_name, "could not compile pattern at offset " + std::to_string((u64)error_offset) + ": " + regex_pcre2_error(error_code));

		pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
	}

	~RegexCode()
	{
		if(code)
			pcre2_code_free(code);
	}
};

struct RegexMatchData {
	pcre2_match_data* data = 0;

	RegexMatchData(pcre2_code* code)
	{
		data = pcre2_match_data_create_from_pattern(code, 0);
		if(!data)
			regex_throw("regex", "could not allocate match data");
	}

	~RegexMatchData()
	{
		if(data)
			pcre2_match_data_free(data);
	}
};

String regex_subject_slice(String subject, PCRE2_SIZE start, PCRE2_SIZE end)
{
	if(start == PCRE2_UNSET || end == PCRE2_UNSET || end < start || start > subject.length())
		return("");
	if(end > subject.length())
		end = subject.length();
	return(subject.substr(start, end - start));
}

size_t regex_next_utf8_offset(String subject, size_t offset)
{
	if(offset >= subject.length())
		return(subject.length() + 1);

	unsigned char c = (unsigned char)subject[offset];
	size_t step = 1;
	if((c & 0x80) == 0)
		step = 1;
	else if((c & 0xE0) == 0xC0)
		step = 2;
	else if((c & 0xF0) == 0xE0)
		step = 3;
	else if((c & 0xF8) == 0xF0)
		step = 4;

	if(offset + step > subject.length())
		step = 1;
	return(offset + step);
}

void regex_add_named_captures(DTree& result, pcre2_code* code, String subject, PCRE2_SIZE* ovector, int rc)
{
	uint32_t name_count = 0;
	uint32_t entry_size = 0;
	PCRE2_SPTR name_table = 0;

	pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &name_count);
	if(name_count == 0)
		return;

	pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &entry_size);
	pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &name_table);

	for(uint32_t i = 0; i < name_count; i += 1)
	{
		PCRE2_SPTR entry = name_table + (i * entry_size);
		uint32_t group_index = (entry[0] << 8) | entry[1];
		String name(reinterpret_cast<const char*>(entry + 2));
		if(group_index >= (uint32_t)rc)
			continue;

		PCRE2_SIZE start = ovector[group_index * 2];
		PCRE2_SIZE end = ovector[group_index * 2 + 1];
		if(start == PCRE2_UNSET || end == PCRE2_UNSET)
			continue;

		result["named"][name] = regex_subject_slice(subject, start, end);
		result["named_offsets"][name]["index"] = (f64)group_index;
		result["named_offsets"][name]["start"] = (f64)start;
		result["named_offsets"][name]["end"] = (f64)end;
	}
}

DTree regex_build_match_tree(String pattern, String flags, String subject, pcre2_code* code, pcre2_match_data* match_data, int rc)
{
	DTree result;
	result["matched"].set_bool(rc >= 0);
	result["pattern"] = pattern;
	result["flags"] = regex_flags_label(flags);

	if(rc < 0)
		return(result);

	PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
	result["start"] = (f64)ovector[0];
	result["end"] = (f64)ovector[1];
	result["match"] = regex_subject_slice(subject, ovector[0], ovector[1]);

	for(int i = 0; i < rc; i += 1)
	{
		DTree capture;
		PCRE2_SIZE start = ovector[i * 2];
		PCRE2_SIZE end = ovector[i * 2 + 1];
		capture["index"] = (f64)i;
		capture["matched"].set_bool(start != PCRE2_UNSET && end != PCRE2_UNSET);
		if(start != PCRE2_UNSET && end != PCRE2_UNSET)
		{
			capture["start"] = (f64)start;
			capture["end"] = (f64)end;
			capture["text"] = regex_subject_slice(subject, start, end);
		}
		result["captures"].push(capture);
	}

	regex_add_named_captures(result, code, subject, ovector, rc);
	return(result);
}

int regex_match_at(RegexCode& regex, RegexMatchData& match_data, String subject, size_t offset, uint32_t options, String function_name)
{
	int rc = pcre2_match(
		regex.code,
		reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
		subject.length(),
		offset,
		options,
		match_data.data,
		0
	);

	if(rc == PCRE2_ERROR_NOMATCH)
		return(rc);
	if(rc < 0)
		regex_throw(function_name, "match failed: " + regex_pcre2_error(rc));
	return(rc);
}

}

bool regex_match(String pattern, String subject, String flags)
{
	RegexCode regex(pattern, flags, "regex_match");
	RegexMatchData match_data(regex.code);
	int rc = regex_match_at(regex, match_data, subject, 0, PCRE2_ANCHORED | PCRE2_ENDANCHORED, "regex_match");
	return(rc >= 0);
}

DTree regex_search(String pattern, String subject, String flags)
{
	RegexCode regex(pattern, flags, "regex_search");
	RegexMatchData match_data(regex.code);
	int rc = regex_match_at(regex, match_data, subject, 0, 0, "regex_search");
	return(regex_build_match_tree(pattern, flags, subject, regex.code, match_data.data, rc));
}

DTree regex_search_all(String pattern, String subject, String flags)
{
	RegexCode regex(pattern, flags, "regex_search_all");
	RegexMatchData match_data(regex.code);
	DTree result;
	result["matched"].set_bool(false);
	result["pattern"] = pattern;
	result["flags"] = regex_flags_label(flags);

	size_t offset = 0;
	while(offset <= subject.length())
	{
		int rc = regex_match_at(regex, match_data, subject, offset, 0, "regex_search_all");
		if(rc == PCRE2_ERROR_NOMATCH)
			break;

		DTree match = regex_build_match_tree(pattern, flags, subject, regex.code, match_data.data, rc);
		result["matches"].push(match);
		result["matched"].set_bool(true);

		PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data.data);
		size_t start = ovector[0];
		size_t end = ovector[1];
		if(end > offset)
			offset = end;
		else
			offset = regex_next_utf8_offset(subject, offset);
	}

	result["count"] = (f64)result["matches"].deref()._map.size();
	return(result);
}

String regex_replace(String pattern, String replacement, String subject, String flags)
{
	RegexCode regex(pattern, flags, "regex_replace");
	PCRE2_SIZE output_length = 0;
	uint32_t options = PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;

	int rc = pcre2_substitute(
		regex.code,
		reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
		subject.length(),
		0,
		options,
		0,
		0,
		reinterpret_cast<PCRE2_SPTR>(replacement.c_str()),
		replacement.length(),
		0,
		&output_length
	);

	if(rc != PCRE2_ERROR_NOMEMORY && rc < 0)
		regex_throw("regex_replace", "substitution failed: " + regex_pcre2_error(rc));

	String output;
	output.resize(output_length);
	rc = pcre2_substitute(
		regex.code,
		reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
		subject.length(),
		0,
		PCRE2_SUBSTITUTE_GLOBAL,
		0,
		0,
		reinterpret_cast<PCRE2_SPTR>(replacement.c_str()),
		replacement.length(),
		reinterpret_cast<PCRE2_UCHAR*>(&output[0]),
		&output_length
	);

	if(rc < 0)
		regex_throw("regex_replace", "substitution failed: " + regex_pcre2_error(rc));

	output.resize(output_length);
	return(output);
}

StringList regex_split(String pattern, String subject, String flags)
{
	RegexCode regex(pattern, flags, "regex_split");
	RegexMatchData match_data(regex.code);
	StringList result;

	size_t offset = 0;
	size_t last_end = 0;
	while(offset <= subject.length())
	{
		int rc = regex_match_at(regex, match_data, subject, offset, 0, "regex_split");
		if(rc == PCRE2_ERROR_NOMATCH)
			break;

		PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data.data);
		size_t start = ovector[0];
		size_t end = ovector[1];
		result.push_back(subject.substr(last_end, start - last_end));
		last_end = end;

		if(end > offset)
			offset = end;
		else
			offset = regex_next_utf8_offset(subject, offset);
	}

	result.push_back(subject.substr(last_end));
	return(result);
}

String trim(String raw)
{
	s64 len = raw.length();
	s64 start_pos = 0;
	s64 end_pos = len - 1;
	if(len == 0 || (len == 1 && isspace((unsigned char)raw[0])))
		return("");
	while(start_pos < len && isspace((unsigned char)raw[start_pos]))
		start_pos++;
	while(end_pos >= 0 && isspace((unsigned char)raw[end_pos]))
		end_pos--;
	if(end_pos < start_pos)
		return("");
	return(raw.substr(start_pos, 1 + end_pos - start_pos));
}

StringList split_space(String str)
{
	StringList result;
	String current_token = "";
	for(auto c : str)
	{
		if(isspace((unsigned char)c))
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
	if(delim == "")
	{
		result.push_back(str);
		return(result);
	}
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

StringMap split_kv(String s, char separator, bool trim_whitespace, bool uppercase_keys)
{
	StringMap result;
	String k;
	String v;
	for(auto s : split(s, "\n"))
	{
		u8 mode = 0;
		k = "";
		v = "";
		if(s == "" || s[0] == '#')
			continue;
		for(auto c : s)
		{
			if(mode == 0)
			{
				if(c == separator)
					mode = 1;
				else
					k.append(1, uppercase_keys ? toupper((unsigned char)c) : c);
			}
			else
			{
				v.append(1, c);
			}
		}
		if(k != "")
		{
			if(trim_whitespace)
				result[trim(k)] = trim(v);
			else
				result[k] = v;
		}
	}
	return(result);
}

StringMap split_http_headers(String s)
{
	StringMap result;
	StringList lines = split(s, "\n");
	if(lines.size() == 0)
		return(result);

	u64 header_start = 0;
	while(header_start < lines.size() && trim(lines[header_start]) == "")
		header_start++;

	if(header_start < lines.size() && lines[header_start].find(':') == String::npos)
	{
		String request_line = trim(lines[header_start]);
		result["REQUEST_METHOD"] = nibble(request_line, " ");
		result["REQUEST_URI"] = nibble(request_line, " ");
		String query_string = result["REQUEST_URI"];
		String base_uri = nibble(query_string, "?");
		result["SERVER_PROTOCOL"] = trim(request_line);
		result["SCRIPT_NAME"] = base_uri;
		result["DOCUMENT_URI"] = base_uri;
		result["QUERY_STRING"] = query_string;
		header_start++;
	}

	for(u64 i = header_start; i < lines.size(); i++)
	{
		String line = lines[i];
		size_t colon = line.find(':');
		if(colon == String::npos)
			continue;
		String header_key = to_upper(trim(line.substr(0, colon)));
		String value = trim(line.substr(colon + 1));
		if(header_key == "")
			continue;
		std::replace(header_key.begin(), header_key.end(), '-', '_');
		result["HTTP_" + header_key] = value;
		if(header_key == "CONTENT_TYPE" || header_key == "CONTENT_LENGTH")
			result[header_key] = value;
	}
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

namespace {

bool array_merge_key_is_index(String key)
{
	if(key == "")
		return(false);
	for(auto c : key)
	{
		if(!isdigit(c))
			return(false);
	}
	return(true);
}

}

StringMap array_merge(StringMap a, StringMap b)
{
	StringMap result = a;
	for(const auto& entry : b)
		result[entry.first] = entry.second;
	return(result);
}

DTree array_merge(DTree a, DTree b)
{
	const DTree& left = a.deref();
	const DTree& right = b.deref();
	if(left.type != 'M')
		return(b);

	DTree result;
	result.set(a);
	if(right.type != 'M')
		return(result);

	bool append_numeric_keys = left.is_list() || right.is_list();
	for(const auto& entry : right._map)
	{
		if(append_numeric_keys && array_merge_key_is_index(entry.first))
		{
			DTree child = entry.second;
			result.push(child);
		}
		else
		{
			result[entry.first] = entry.second;
		}
	}
	return(result);
}

StringList split_utf8(String s, bool compound_characters)
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
	if(compound_characters)
	{
		StringList compound_result;
		bool join_next = false;
		bool last_was_regional = false;
		for(auto& s : result)
		{
			if(join_next)
			{
				compound_result[compound_result.size()-1] += s;
				join_next = false;
			}
			else if(s == "\xE2" "\x80" "\x8D") // ZWJ
			{
				compound_result[compound_result.size()-1] += s;
				join_next = true;
				last_was_regional = false;
			}
			else if(s[0] == '\xF0' && s[1] == '\x9F' && s[2] == '\x87' && s[3] >= '\xA6' && s[3] <= '\xBF') // Regional indicator letters
			{
				if(last_was_regional)
				{
					compound_result[compound_result.size()-1] += s;
					last_was_regional = false;
				}
				else
				{
					compound_result.push_back(s);
					last_was_regional = true;
				}
			}
			else if(s[0] == '\xEF' && s[1] == '\xB8' && s[2] >= '\x80' && s[2] <= '\x8F') // Variation selector
			{
				compound_result[compound_result.size()-1] += s;
				last_was_regional = false;
			}
			else
			{
				compound_result.push_back(s);
				last_was_regional = false;
			}
		}
		return(compound_result);
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
			case('\''):
				result.append("&#39;");
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

String json_encode(String s, char quote_char)
{
	return(json_escape(s, quote_char));
}

u64 int_val(String s, u32 base)
{
	return(strtol(s.c_str(), 0, base));
}

f64 float_val(String s)
{
	return(strtod(s.c_str(), 0));
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

String json_encode(DTree t, char quote_char)
{
	String result = "";
	if(t.is_array())
	{
		if(t.is_list())
		{
			result += "[";
			u32 count = 0;
			for(u32 i = 0; i < t.deref()._map.size(); i += 1)
			{
				if(count > 0)
					result += ", ";
				count += 1;
				auto it = t.deref()._map.find(std::to_string(i));
				if(it == t.deref()._map.end())
				{
					result += "null";
					continue;
				}
				result += json_encode(it->second, quote_char);
			}
			result += "]";
		}
		else
		{
			result += "{";
			u32 count = 0;
			t.each([&] (DTree item, String key) {
				if(count > 0)
					result += ", ";
				count += 1;
				result += json_escape(key, quote_char) + ": " + json_encode(item, quote_char);
			});
			result += "}";
		}
	}
	else
	{
		result = t.to_json(quote_char);
	}
	return(result);
}

namespace {

String xml_escape_text(String s)
{
	String result;
	for(char c : s)
	{
		switch(c)
		{
			case('&'):
				result += "&amp;";
				break;
			case('<'):
				result += "&lt;";
				break;
			case('>'):
				result += "&gt;";
				break;
			default:
				result.append(1, c);
				break;
		}
	}
	return(result);
}

String xml_escape_attr(String s)
{
	String result;
	for(char c : s)
	{
		switch(c)
		{
			case('&'):
				result += "&amp;";
				break;
			case('<'):
				result += "&lt;";
				break;
			case('>'):
				result += "&gt;";
				break;
			case('"'):
				result += "&quot;";
				break;
			case('\''):
				result += "&apos;";
				break;
			default:
				result.append(1, c);
				break;
		}
	}
	return(result);
}

bool xml_name_start_char(char c)
{
	return(isalpha((unsigned char)c) || c == '_' || c == ':');
}

bool xml_name_char(char c)
{
	return(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == ':');
}

String xml_safe_name(String raw, String fallback = "item")
{
	raw = trim(raw);
	if(raw == "")
		return(fallback);

	String result;
	for(char c : raw)
	{
		if(xml_name_char(c))
			result.append(1, c);
		else
			result.append(1, '_');
	}

	if(result == "" || !xml_name_start_char(result[0]))
		result = "_" + result;
	return(result);
}

String xml_tree_scalar_text(DTree t)
{
	const DTree& target = t.deref();
	if(target.type == 'B')
		return(target._bool ? "true" : "false");
	return(t.to_string());
}

const DTree* xml_tree_key(const DTree& t, String key)
{
	return(t.deref().key(key));
}

bool xml_tree_has_element_shape(const DTree& t)
{
	const DTree& target = t.deref();
	return(target.type == 'M' && target.key("name"));
}

String xml_encode_element(DTree tree, String name_hint)
{
	const DTree& target = tree.deref();
	bool shaped = xml_tree_has_element_shape(target);
	String name = xml_safe_name(name_hint, "root");

	if(shaped)
	{
		const DTree* name_node = xml_tree_key(target, "name");
		DTree name_copy = *name_node;
		name = xml_safe_name(name_copy.to_string(), name);
	}

	String result = "<" + name;

	if(shaped)
	{
		const DTree* attrs = xml_tree_key(target, "attrs");
		if(attrs && attrs->deref().type == 'M')
		{
			for(const auto& entry : attrs->deref()._map)
			{
				DTree attr_value = entry.second;
				result += " " + xml_safe_name(entry.first, "attr") + "=\"" + xml_escape_attr(xml_tree_scalar_text(attr_value)) + "\"";
			}
		}
	}

	String content;
	if(shaped)
	{
		const DTree* text = xml_tree_key(target, "text");
		if(text)
		{
			DTree text_copy = *text;
			content += xml_escape_text(xml_tree_scalar_text(text_copy));
		}

		const DTree* children = xml_tree_key(target, "children");
		if(children && children->deref().type == 'M')
		{
			const DTree& child_map = children->deref();
			if(child_map.is_list())
			{
				for(u32 i = 0; i < child_map._map.size(); i += 1)
				{
					auto it = child_map._map.find(std::to_string(i));
					if(it != child_map._map.end())
						content += xml_encode_element(it->second, "item");
				}
			}
			else
			{
				for(const auto& entry : child_map._map)
					content += xml_encode_element(entry.second, entry.first);
			}
		}
	}
	else if(target.type == 'M')
	{
		if(target.is_list())
		{
			for(u32 i = 0; i < target._map.size(); i += 1)
			{
				auto it = target._map.find(std::to_string(i));
				if(it != target._map.end())
					content += xml_encode_element(it->second, "item");
			}
		}
		else
		{
			for(const auto& entry : target._map)
				content += xml_encode_element(entry.second, entry.first);
		}
	}
	else
	{
		content += xml_escape_text(xml_tree_scalar_text(tree));
	}

	if(content == "")
		return(result + "/>");
	return(result + ">" + content + "</" + name + ">");
}

String xml_utf8_from_codepoint(u64 codepoint)
{
	String result;
	if(codepoint == 0 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
		return(result);
	if(codepoint <= 0x7f)
	{
		result.append(1, (char)codepoint);
	}
	else if(codepoint <= 0x7ff)
	{
		result.append(1, (char)(0xc0 | (codepoint >> 6)));
		result.append(1, (char)(0x80 | (codepoint & 0x3f)));
	}
	else if(codepoint <= 0xffff)
	{
		result.append(1, (char)(0xe0 | (codepoint >> 12)));
		result.append(1, (char)(0x80 | ((codepoint >> 6) & 0x3f)));
		result.append(1, (char)(0x80 | (codepoint & 0x3f)));
	}
	else
	{
		result.append(1, (char)(0xf0 | (codepoint >> 18)));
		result.append(1, (char)(0x80 | ((codepoint >> 12) & 0x3f)));
		result.append(1, (char)(0x80 | ((codepoint >> 6) & 0x3f)));
		result.append(1, (char)(0x80 | (codepoint & 0x3f)));
	}
	return(result);
}

String xml_decode_entity(String entity)
{
	if(entity == "amp")
		return("&");
	if(entity == "lt")
		return("<");
	if(entity == "gt")
		return(">");
	if(entity == "quot")
		return("\"");
	if(entity == "apos")
		return("'");
	if(entity.length() > 1 && entity[0] == '#')
	{
		u32 base = 10;
		String digits = entity.substr(1);
		if(digits.length() > 1 && (digits[0] == 'x' || digits[0] == 'X'))
		{
			base = 16;
			digits = digits.substr(1);
		}
		u64 codepoint = int_val(digits, base);
		String utf8 = xml_utf8_from_codepoint(codepoint);
		if(utf8 != "")
			return(utf8);
	}
	return("&" + entity + ";");
}

String xml_decode_text(String s)
{
	String result;
	for(u32 i = 0; i < s.length(); i += 1)
	{
		if(s[i] == '&')
		{
			auto end = s.find(";", i + 1);
			if(end != String::npos)
			{
				result += xml_decode_entity(s.substr(i + 1, end - i - 1));
				i = end;
				continue;
			}
		}
		result.append(1, s[i]);
	}
	return(result);
}

void xml_throw(String message)
{
	throw std::runtime_error("xml_decode(): " + message);
}

struct XmlParser
{
	String src;
	u32 i = 0;

	XmlParser(String source)
	{
		src = source;
	}

	bool starts_with(String token)
	{
		return(src.compare(i, token.length(), token) == 0);
	}

	void skip_space()
	{
		while(i < src.length() && isspace((unsigned char)src[i]))
			i += 1;
	}

	void skip_until(String token)
	{
		auto end = src.find(token, i);
		if(end == String::npos)
		{
			i = src.length();
			return;
		}
		i = end + token.length();
	}

	void skip_misc()
	{
		bool again = true;
		while(again)
		{
			again = false;
			skip_space();
			if(starts_with("<?"))
			{
				skip_until("?>");
				again = true;
			}
			else if(starts_with("<!--"))
			{
				skip_until("-->");
				again = true;
			}
			else if(starts_with("<!DOCTYPE") || starts_with("<!doctype"))
			{
				skip_until(">");
				again = true;
			}
		}
	}

	String read_name()
	{
		if(i >= src.length() || !xml_name_start_char(src[i]))
			xml_throw("expected XML name at byte " + std::to_string((u64)i));
		u32 start = i;
		i += 1;
		while(i < src.length() && xml_name_char(src[i]))
			i += 1;
		return(src.substr(start, i - start));
	}

	String read_quoted_value()
	{
		skip_space();
		if(i >= src.length() || (src[i] != '"' && src[i] != '\''))
			xml_throw("expected quoted attribute value at byte " + std::to_string((u64)i));
		char quote = src[i];
		i += 1;
		u32 start = i;
		while(i < src.length() && src[i] != quote)
			i += 1;
		if(i >= src.length())
			xml_throw("unterminated attribute value");
		String value = src.substr(start, i - start);
		i += 1;
		return(xml_decode_text(value));
	}

	String read_text_until_markup()
	{
		u32 start = i;
		while(i < src.length() && src[i] != '<')
			i += 1;
		return(xml_decode_text(src.substr(start, i - start)));
	}

	DTree parse_element()
	{
		if(i >= src.length() || src[i] != '<')
			xml_throw("expected element at byte " + std::to_string((u64)i));
		i += 1;

		DTree node;
		String name = read_name();
		node["name"] = name;

		while(i < src.length())
		{
			skip_space();
			if(starts_with("/>"))
			{
				i += 2;
				return(node);
			}
			if(starts_with(">"))
			{
				i += 1;
				break;
			}

			String attr_name = read_name();
			skip_space();
			if(i >= src.length() || src[i] != '=')
				xml_throw("expected '=' after attribute " + attr_name);
			i += 1;
			node["attrs"][attr_name] = read_quoted_value();
		}

		String text;
		while(i < src.length())
		{
			if(starts_with("</"))
			{
				i += 2;
				String close_name = read_name();
				skip_space();
				if(i >= src.length() || src[i] != '>')
					xml_throw("expected '>' after closing tag " + close_name);
				i += 1;
				if(close_name != name)
					xml_throw("mismatched closing tag: expected " + name + " but found " + close_name);
				if(trim(text) != "")
					node["text"] = text;
				return(node);
			}

			if(starts_with("<!--"))
			{
				skip_until("-->");
				continue;
			}

			if(starts_with("<![CDATA["))
			{
				i += 9;
				auto end = src.find("]]>", i);
				if(end == String::npos)
					xml_throw("unterminated CDATA section");
				text += src.substr(i, end - i);
				i = end + 3;
				continue;
			}

			if(starts_with("<?"))
			{
				skip_until("?>");
				continue;
			}

			if(src[i] == '<')
			{
				DTree child = parse_element();
				node["children"].push(child);
				continue;
			}

			String chunk = read_text_until_markup();
			if(trim(chunk) != "")
				text += chunk;
		}

		xml_throw("missing closing tag for " + name);
		return(node);
	}

	DTree parse_document()
	{
		skip_misc();
		if(i >= src.length())
			xml_throw("empty document");
		DTree result = parse_element();
		skip_misc();
		if(i < src.length())
			xml_throw("unexpected content after root element at byte " + std::to_string((u64)i));
		return(result);
	}
};

}

String xml_encode(DTree t, String root_name)
{
	return(xml_encode_element(t, root_name));
}

DTree xml_decode(String s)
{
	XmlParser parser(s);
	return(parser.parse_document());
}

namespace {

String yaml_indent(u32 width)
{
	return(String(width, ' '));
}

String yaml_rtrim(String raw)
{
	while(raw.length() > 0)
	{
		char c = raw[raw.length() - 1];
		if(c != ' ' && c != '\t' && c != '\r')
			break;
		raw.erase(raw.length() - 1);
	}
	return(raw);
}

u32 yaml_count_indent(String raw)
{
	u32 result = 0;
	while(result < raw.length() && raw[result] == ' ')
		result += 1;
	return(result);
}

bool yaml_is_plain_key(String key)
{
	if(key == "")
		return(false);
	for(char c : key)
	{
		if(!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
			return(false);
	}
	return(true);
}

bool yaml_is_list_item(String content)
{
	return(content == "-" || str_starts_with(content, "- "));
}

bool yaml_is_number_like(String raw)
{
	if(raw == "")
		return(false);
	char* end = 0;
	strtod(raw.c_str(), &end);
	if(end == raw.c_str())
		return(false);
	while(end && *end != 0)
	{
		if(!isspace((unsigned char)*end))
			return(false);
		end += 1;
	}
	return(true);
}

bool yaml_plain_scalar_safe(String raw)
{
	if(raw == "")
		return(false);
	String lower = to_lower(raw);
	if(lower == "true" || lower == "false" || lower == "null" || lower == "~" || lower == "yes" || lower == "no" || lower == "on" || lower == "off")
		return(false);
	if(yaml_is_number_like(raw))
		return(false);
	if(raw[0] == '-' || raw[0] == '?' || raw[0] == ':' || raw[0] == '@' || raw[0] == '`' || raw[0] == '!' || raw[0] == '&' || raw[0] == '*' || raw[0] == '#')
		return(false);
	for(u32 i = 0; i < raw.length(); i += 1)
	{
		char c = raw[i];
		if(c == '\n' || c == '\r' || c == '\t')
			return(false);
		if(c == '[' || c == ']' || c == '{' || c == '}' || c == ',' || c == '"' || c == '\'')
			return(false);
		if(c == '#')
		{
			if(i == 0 || isspace((unsigned char)raw[i - 1]))
				return(false);
		}
		if(c == ':')
		{
			if(i + 1 >= raw.length() || isspace((unsigned char)raw[i + 1]))
				return(false);
		}
	}
	return(true);
}

String yaml_double_quote(String raw)
{
	String result = "\"";
	for(char c : raw)
	{
		switch(c)
		{
			case('\\'):
				result += "\\\\";
				break;
			case('"'):
				result += "\\\"";
				break;
			case('\n'):
				result += "\\n";
				break;
			case('\r'):
				result += "\\r";
				break;
			case('\t'):
				result += "\\t";
				break;
			default:
				result.append(1, c);
				break;
		}
	}
	result += "\"";
	return(result);
}

String yaml_quote_key(String key)
{
	if(yaml_is_plain_key(key))
		return(key);
	return(yaml_double_quote(key));
}

String yaml_scalar_to_string(DTree t)
{
	const DTree& target = t.deref();
	switch(target.type)
	{
		case('B'):
			return(target._bool ? "true" : "false");
		case('F'):
		{
			String value = std::to_string(target._float);
			if(value.find(".") != String::npos)
			{
				while(value.length() > 0 && value[value.length() - 1] == '0')
					value.erase(value.length() - 1);
				if(value.length() > 0 && value[value.length() - 1] == '.')
					value.erase(value.length() - 1);
			}
			if(value == "-0")
				value = "0";
			return(value);
		}
		case('P'):
			return(std::to_string((u64)target._ptr));
		default:
			return(t.to_string());
	}
}

String yaml_encode_scalar(DTree t, u32 indent)
{
	String raw = yaml_scalar_to_string(t);
	const DTree& target = t.deref();
	if(target.type == 'B' || target.type == 'F')
		return(raw);
	if(raw.find("\n") != String::npos)
	{
		String result = "|\n";
		u32 start = 0;
		while(start <= raw.length())
		{
			auto end = raw.find("\n", start);
			String line = (end == String::npos) ? raw.substr(start) : raw.substr(start, end - start);
			result += yaml_indent(indent + 2) + line + "\n";
			if(end == String::npos)
				break;
			start = end + 1;
		}
		return(result.substr(0, result.length() - 1));
	}
	if(yaml_plain_scalar_safe(raw))
		return(raw);
	return(yaml_double_quote(raw));
}

String yaml_encode_node(DTree t, u32 indent)
{
	const DTree& target = t.deref();
	String result;
	if(target.type == 'M')
	{
		if(target.is_list())
		{
			for(u32 i = 0; i < target._map.size(); i += 1)
			{
				auto it = target._map.find(std::to_string(i));
				if(it == target._map.end())
					continue;
				const DTree& child = it->second.deref();
				if(child.type == 'M')
				{
					result += yaml_indent(indent) + "-\n";
					result += yaml_encode_node(it->second, indent + 2);
				}
				else
				{
					result += yaml_indent(indent) + "- " + yaml_encode_scalar(it->second, indent) + "\n";
				}
			}
			return(result);
		}

		for(const auto& entry : target._map)
		{
			const DTree& child = entry.second.deref();
			result += yaml_indent(indent) + yaml_quote_key(entry.first) + ":";
			if(child.type == 'M')
			{
				result += "\n" + yaml_encode_node(entry.second, indent + 2);
			}
			else
			{
				result += " " + yaml_encode_scalar(entry.second, indent) + "\n";
			}
		}
		return(result);
	}
	return(yaml_indent(indent) + yaml_encode_scalar(t, indent) + "\n");
}

String yaml_strip_comment(String raw)
{
	bool in_single = false;
	bool in_double = false;
	bool escaped = false;
	for(u32 i = 0; i < raw.length(); i += 1)
	{
		char c = raw[i];
		if(in_double && escaped)
		{
			escaped = false;
			continue;
		}
		if(in_double && c == '\\')
		{
			escaped = true;
			continue;
		}
		if(!in_double && c == '\'')
			in_single = !in_single;
		else if(!in_single && c == '"')
			in_double = !in_double;
		else if(!in_single && !in_double && c == '#' && (i == 0 || isspace((unsigned char)raw[i - 1])))
			return(yaml_rtrim(raw.substr(0, i)));
	}
	return(yaml_rtrim(raw));
}

String yaml_ltrim(String raw)
{
	u32 i = 0;
	while(i < raw.length() && (raw[i] == ' ' || raw[i] == '\t'))
		i += 1;
	return(raw.substr(i));
}

struct YamlLine
{
	String raw;
	String content;
	u32 indent = 0;
	bool empty = false;
};

String yaml_decode_quoted(String raw)
{
	if(raw.length() < 2)
		return(raw);
	char quote = raw[0];
	String body = raw.substr(1, raw.length() - 2);
	if(quote == '\'')
		return(replace(body, "''", "'"));

	String result;
	for(u32 i = 0; i < body.length(); i += 1)
	{
		char c = body[i];
		if(c == '\\' && i + 1 < body.length())
		{
			i += 1;
			switch(body[i])
			{
				case('n'):
					result.append(1, '\n');
					break;
				case('r'):
					result.append(1, '\r');
					break;
				case('t'):
					result.append(1, '\t');
					break;
				case('"'):
					result.append(1, '"');
					break;
				case('\\'):
					result.append(1, '\\');
					break;
				default:
					result.append(1, body[i]);
					break;
			}
		}
		else
		{
			result.append(1, c);
		}
	}
	return(result);
}

void yaml_throw(String message)
{
	throw std::runtime_error("yaml_decode(): " + message);
}

struct YamlParser
{
	std::vector<YamlLine> lines;
	u32 i = 0;

	YamlParser(String source)
	{
		u32 start = 0;
		while(start <= source.length())
		{
			auto end = source.find("\n", start);
			String raw = (end == String::npos) ? source.substr(start) : source.substr(start, end - start);
			if(end == String::npos && raw == "" && start == source.length())
				break;
			if(raw.length() > 0 && raw[raw.length() - 1] == '\r')
				raw.erase(raw.length() - 1);
			YamlLine line;
			line.raw = raw;
			line.indent = yaml_count_indent(raw);
			line.content = yaml_ltrim(yaml_strip_comment(raw));
			line.empty = (trim(line.content) == "");
			lines.push_back(line);
			if(end == String::npos)
				break;
			start = end + 1;
		}
	}

	void skip_empty()
	{
		while(i < lines.size() && lines[i].empty)
			i += 1;
	}

	bool split_key_value(String content, String& key, String& value)
	{
		bool in_single = false;
		bool in_double = false;
		bool escaped = false;
		for(u32 pos = 0; pos < content.length(); pos += 1)
		{
			char c = content[pos];
			if(in_double && escaped)
			{
				escaped = false;
				continue;
			}
			if(in_double && c == '\\')
			{
				escaped = true;
				continue;
			}
			if(!in_double && c == '\'')
				in_single = !in_single;
			else if(!in_single && c == '"')
				in_double = !in_double;
			else if(!in_single && !in_double && c == ':' && (pos + 1 >= content.length() || isspace((unsigned char)content[pos + 1])))
			{
				key = trim(content.substr(0, pos));
				value = trim(content.substr(pos + 1));
				if(key == "")
					yaml_throw("empty mapping key at line " + std::to_string((u64)i + 1));
				if((key[0] == '"' && key[key.length() - 1] == '"') || (key[0] == '\'' && key[key.length() - 1] == '\''))
					key = yaml_decode_quoted(key);
				return(true);
			}
		}
		return(false);
	}

	DTree parse_scalar(String value)
	{
		DTree result;
		value = trim(value);
		if(value == "")
		{
			result = "";
			return(result);
		}
		if((value[0] == '"' && value[value.length() - 1] == '"') || (value[0] == '\'' && value[value.length() - 1] == '\''))
		{
			result = yaml_decode_quoted(value);
			return(result);
		}
		String lower = to_lower(value);
		if(lower == "true")
		{
			result.set_bool(true);
			return(result);
		}
		if(lower == "false")
		{
			result.set_bool(false);
			return(result);
		}
		if(lower == "null" || lower == "~")
		{
			result = "";
			return(result);
		}
		result = value;
		return(result);
	}

	String parse_block_scalar(u32 parent_indent, String style)
	{
		String literal;
		bool found = false;
		u32 content_indent = parent_indent + 2;
		while(i < lines.size())
		{
			String raw = lines[i].raw;
			if(trim(raw) == "")
			{
				if(found)
					literal += "\n";
				i += 1;
				continue;
			}
			u32 indent = yaml_count_indent(raw);
			if(indent <= parent_indent)
				break;
			if(!found)
			{
				content_indent = indent;
				found = true;
			}
			if(indent < content_indent)
				break;
			String text = raw.substr(std::min((u32)raw.length(), content_indent));
			literal += text + "\n";
			i += 1;
		}
		if(literal.length() > 0)
			literal.erase(literal.length() - 1);
		if(style == ">")
		{
			String folded;
			bool last_newline = false;
			for(char c : literal)
			{
				if(c == '\n')
				{
					if(last_newline)
						folded += "\n";
					else
						folded += " ";
					last_newline = true;
				}
				else
				{
					folded.append(1, c);
					last_newline = false;
				}
			}
			return(folded);
		}
		return(literal);
	}

	DTree parse_value_after_line(String value, u32 parent_indent)
	{
		if(value == "|" || value == ">")
		{
			DTree result;
			result = parse_block_scalar(parent_indent, value);
			return(result);
		}
		if(value == "")
		{
			skip_empty();
			if(i < lines.size() && lines[i].indent > parent_indent)
				return(parse_block(lines[i].indent));
			DTree result;
			result = "";
			return(result);
		}
		return(parse_scalar(value));
	}

	void merge_map(DTree& target, DTree source)
	{
		if(source.deref().type != 'M' || source.deref().is_list())
			return;
		for(const auto& entry : source.deref()._map)
			target[entry.first] = entry.second;
	}

	DTree parse_list(u32 indent)
	{
		DTree result;
		result.set_array();
		while(i < lines.size())
		{
			skip_empty();
			if(i >= lines.size() || lines[i].indent < indent)
				break;
			if(lines[i].indent != indent || !yaml_is_list_item(lines[i].content))
				break;

			String after = trim(lines[i].content.substr(1));
			u32 line_indent = lines[i].indent;
			i += 1;

			DTree item;
			if(after == "")
			{
				skip_empty();
				if(i < lines.size() && lines[i].indent > line_indent)
					item = parse_block(lines[i].indent);
				else
					item = "";
			}
			else
			{
				String key;
				String value;
				if(split_key_value(after, key, value))
				{
					item[key] = parse_value_after_line(value, line_indent);
					skip_empty();
					if(i < lines.size() && lines[i].indent > line_indent)
						merge_map(item, parse_block(lines[i].indent));
				}
				else
				{
					item = parse_scalar(after);
				}
			}
			result.push(item);
		}
		return(result);
	}

	DTree parse_map(u32 indent)
	{
		DTree result;
		result.set_type('M');
		while(i < lines.size())
		{
			skip_empty();
			if(i >= lines.size() || lines[i].indent < indent)
				break;
			if(lines[i].indent != indent || yaml_is_list_item(lines[i].content))
				break;

			String key;
			String value;
			if(!split_key_value(lines[i].content, key, value))
			{
				if(result.deref()._map.size() == 0)
				{
					DTree scalar = parse_scalar(lines[i].content);
					i += 1;
					return(scalar);
				}
				yaml_throw("expected mapping entry at line " + std::to_string((u64)i + 1));
			}
			u32 line_indent = lines[i].indent;
			i += 1;
			result[key] = parse_value_after_line(value, line_indent);
		}
		return(result);
	}

	DTree parse_block(u32 indent)
	{
		skip_empty();
		if(i >= lines.size())
		{
			DTree empty;
			empty.set_type('M');
			return(empty);
		}
		if(lines[i].indent < indent)
		{
			DTree empty;
			empty.set_type('M');
			return(empty);
		}
		if(yaml_is_list_item(lines[i].content))
			return(parse_list(lines[i].indent));
		return(parse_map(lines[i].indent));
	}

	DTree parse_document()
	{
		skip_empty();
		if(i < lines.size() && lines[i].content == "---")
			i += 1;
		DTree result = parse_block(i < lines.size() ? lines[i].indent : 0);
		skip_empty();
		if(i < lines.size() && lines[i].content == "...")
			i += 1;
		skip_empty();
		if(i < lines.size())
			yaml_throw("unexpected content at line " + std::to_string((u64)i + 1));
		return(result);
	}
};

}

String yaml_encode(DTree t)
{
	return(yaml_encode_node(t, 0));
}

DTree yaml_decode(String s)
{
	YamlParser parser(s);
	return(parser.parse_document());
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
DTree json_decode_array(String s, u32& i);

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
	else if(c == '[')
	{
		i += 1;
		return(json_decode_array(s, i));
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

DTree json_decode_array(String s, u32& i)
{
	DTree result;
	result.set_array();
	json_consume_space(s, i);
	while(i < s.length())
	{
		char c = s[i];
		if(c == ']')
		{
			i += 1;
			return(result);
		}
		else if(c == ',')
		{
			i += 1;
		}
		else
		{
			DTree v = json_decode_value(s, i);
			result.push(v);
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
	{
		ob_start();
	}
	else
	{
		context->ob = context->ob_stack.back();
	}
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

String safe_name(String raw)
{
	return(ascii_safe_name(raw));
}

String ascii_safe_name(String raw)
{
	String result = "";
	for(auto c : raw)
	{
		if(isalnum(c) || c == '_')
			result.append(1, c);
	}
	return(result);
}
