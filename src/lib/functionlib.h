
u8 char_to_u8(char input);
u8 hex_to_u8(String src);
u64 int_val(String s, u32 base = 10);
String to_lower(String s);
String to_upper(String s);
String replace(String s, String search, String replace_with);

String trim(String raw);
StringList split(String str);
StringList split(String str, String delim);
String join(StringList l, String delim = "\n");
String nibble(String& haystack, String delim);
void json_consume_space(String s, u32& i);
StringList split_utf8(String s);

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

#define is_bit_set(var,pos) ((var) & (1<<(pos)))
