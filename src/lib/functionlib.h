
u8 char_to_u8(char input);
u8 hex_to_u8(String src);
u64 int_val(String s, u32 base = 10);
f64 float_val(String s);
String to_lower(String s);
String to_upper(String s);
String replace(String s, String search, String replace_with);

String trim(String raw);
StringList split_space(String str);
StringList split(String str, String delim);
StringList split_utf8(String s, bool compound_characters = false);
StringMap split_kv(String s, char separator = '=', bool trim_whitespace = true);
String join(StringList l, String delim = "\n");
String nibble(String& haystack, String delim);
void json_consume_space(String s, u32& i);

String to_string(StringList l) {
	String result;
	u32 i = 0;
	for(auto& s : l)
	{
		if(i > 0)
			result.append("\n");
		result.append(s);
		i += 1;
	}
	return(result);
}

String to_string(SharedUnit* u) {
	String result;

	result += String("SharedUnit( \n")+
		"Source:"+(u->file_name)+"\n"+
		"SharedObject:"+(u->so_name)+"\n"+
		"API:"+(u->api_file_name)+"\n"+
		to_string(u->api_declarations);

	return(result);
}

template <typename ITYPE>
String to_hex(ITYPE w, size_t hex_len = sizeof(ITYPE)<<1)
{
    static const char* digits = "0123456789ABCDEF";
    String rc(hex_len,'0');
    for (size_t i=0, j=(hex_len-1)*4 ; i<hex_len; ++i,j-=4)
        rc[i] = digits[(w>>j) & 0x0f];
    return(rc);
}

template<typename T>
std::vector<T> filter(std::vector<T> items, std::function<bool (T)> f)
{
	std::vector<T> new_items;
	for(auto item : items)
	{
		if(f(item))
			new_items.push_back(item);
	}
	return(new_items);
}

template <class ...Args>
String first(Args... args)
{
    std::vector<String> vec = {args...};
    for(auto s : vec)
		if(trim(s) != "")
			return(s);
	return("");
}

String html_escape(String s);
String html_escape(u64 a);
String html_escape(f64 a);

String json_encode(DTree t);
DTree json_decode(String s);

String var_dump(StringMap map, String prefix = "", String postfix = "\n");
String var_dump(StringList slist, String prefix = "", String postfix = "\n");

void ob_start();
void ob_clear();
String ob_get_clear();
String ob_get();
String safe_name(String raw);

#define is_bit_set(var,pos) ((var) & (1<<(pos)))
