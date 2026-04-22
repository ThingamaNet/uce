#pragma once

String json_escape(String s, char quote_char = '"');

// DTree is UCE's general-purpose structured value container.
// It stores scalar values, nested map/list-like values, and internal references.
// Numeric and boolean reads are intentionally permissive so request data,
// JSON-decoded values, and metadata trees can be consumed without repetitive
// manual parsing at each call site.
struct DTree {

	char type = 'S';

	String 	_String;
	f64 	_float;
	s64 	_array_index = 0;
	bool	_bool;
	bool	_list_mode = false;
	void*	_ptr;
	std::map<String, DTree> _map;

	void each(std::function <void (DTree t, String key)> f);
	bool is_array();
	bool is_list() const;
	String to_string();
	s64 to_s64();
	u64 to_u64();
	f64 to_f64();
	bool to_bool();
	StringMap to_stringmap();
	String to_json(char quote_char = '"');
	String get_type_name();
	DTree get_by_path(String path, String delim = "/");
	bool is_reference();
	DTree* reference_target();
	const DTree* reference_target() const;
	DTree& deref();
	const DTree& deref() const;
	void set_type(char t);
	void set(String s);
	void set(void* p);
	void set(f64 f);
	void set_bool(bool b);
	void set(DTree source);
	void set(StringMap source);
	void set_array();
	void set_reference(DTree* target);
	bool has(String s) const;
	DTree* key(String s);
	const DTree* key(String s) const;
	DTree* get_or_create(String s);
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
