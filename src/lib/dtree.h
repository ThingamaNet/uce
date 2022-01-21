String json_escape(String s);

struct DTree {

	char type = 'S';

	String 	_String;
	f64 	_float;
	s64 	_array_index;
	bool	_bool;
	void*	_ptr;
	std::map<String, DTree> _map;

	void each(std::function <void (DTree t, String key)> f);
	bool is_array();
	String to_string();
	String to_json();
	String get_type_name();
	void set_type(char t);
	void set(String s);
	void set(void* p);
	void set(f64 f);
	void set_bool(bool b);
	void set(DTree source);
	void set(StringMap source);
	DTree* key(String s);
	DTree& operator [] (String s);
	void operator = (String v);
	void operator = (f64 v);
	void operator = (void* v);
	void operator = (DTree v);
	void operator = (StringMap v);

	void push(DTree& child);
	DTree pop();
	void remove(String s);
	void clear();
};

String to_String(DTree t);
String var_dump(DTree map, String prefix = "", String postfix = "\n");
