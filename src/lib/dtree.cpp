
void DTree::each(std::function <void (DTree t, String key)> f)
{
	switch(type)
	{
		case('M'):
			for (auto it = _map.begin(); it != _map.end(); ++it)
			{
				f(it->second, it->first);
			}
			break;
		default:
			f(*this, "");
			break;
	}
}

bool DTree::is_array()
{
	return(type == 'M');
}

String DTree::to_string()
{
	switch(type)
	{
		case('S'):
			return(_String);
			break;
		case('F'):
			return(std::to_string(_float));
			break;
		case('B'):
			return(_bool ? "(true)" : "(false)");
			break;
		case('M'):
			return("");
			break;
		case('P'):
			return(std::to_string((u64)_ptr));
			break;
	}
}

String DTree::to_json()
{
	switch(type)
	{
		case('S'):
			return(json_escape(_String));
			break;
		case('F'):
			return(std::to_string(_float));
			break;
		case('B'):
			return(_bool ? "true" : "false");
			break;
		case('M'):
			return("\"(array)\"");
			break;
		case('P'):
			return("\"(pointer)\"");
			break;
	}
}

String DTree::get_type_name()
{
	switch(type)
	{
		case('S'):
			return("String");
			break;
		case('F'):
			return("f64");
			break;
		case('B'):
			return("bool");
			break;
		case('M'):
			return("array");
			break;
		case('P'):
			return("pointer");
			break;
	}
}

void DTree::set_type(char t)
{
	if(type != t)
	{
		type = t;
		switch(type)
		{
			case('M'):
				_map.clear();
				_array_index = 0;
				break;
		}
	}
}

void DTree::set(String s)
{
	set_type('S');
	_String = s;
}

void DTree::set(void* p)
{
	set_type('P');
	_ptr = p;
}

void DTree::set(f64 f)
{
	set_type('F');
	_float = f;
}

void DTree::set_bool(bool b)
{
	set_type('B');
	_bool = b;
}

void DTree::set(DTree source)
{
	set_type(source.type);
	switch(type)
	{
		case('S'):
			_String = source._String;
			break;
		case('F'):
			_float = source._float;
			break;
		case('B'):
			_bool = source._bool;
			break;
		case('M'):
			_map = source._map;
			break;
		case('P'):
			_ptr = source._ptr;
			break;
	}
}

void DTree::set(StringMap source)
{
	set_type('M');
	for (auto it = source.begin(); it != source.end(); ++it)
	{
		_map[it->first] = it->second;
	}
}

DTree* DTree::key(String s)
{
	set_type('M');
	return(&_map[s]);
}

DTree& DTree::operator [] (String s) {
	set_type('M');
	return(_map[s]);
}

void DTree::operator = (String v) { set(v); }
void DTree::operator = (f64 v) { set(v); }
void DTree::operator = (void* v) { set(v); }
void DTree::operator = (DTree v) { set(v); }
void DTree::operator = (StringMap v) { set(v); }

void DTree::push(DTree& child)
{
	set_type('M');
	_map[std::to_string(_array_index)] = child;
	_array_index += 1;
}

DTree DTree::pop()
{
	set_type('M');
	auto last = _map.rbegin();
	DTree result = last->second;
	_map.erase(last->first);
	return(result);
}

void DTree::remove(String s)
{
	set_type('M');
	_map.erase(s);
}

void DTree::clear()
{
	set_type('M');
	_map.clear();
}

String to_String(DTree t)
{
	return(t.to_string());
}

String var_dump(DTree map, String prefix, String postfix)
{
	String result = "";
	if(!map.is_array())
		return(map.to_string());
	map.each([&] (DTree item, String key) {
		result += prefix + key + ": " + item.to_string() + postfix;
		if(item.is_array())
			result += var_dump(item, prefix + "\t");
	});
	return(result);
}

String json_escape(String s)
{
	//return(String("\"")+s+"\"");
	String result;
	u32 i = 0;
	result.append(1, '"');
	while(i < s.length())
	{
		char c = s[i];
		switch(c)
		{
			case('\t'):
				result.append("\\t");
				break;
			case('\n'):
				result.append("\\n");
				break;
			case('"'):
				result.append("\\\"");
				break;
			case('\r'):
				result.append("\\r");
				break;
			case('\\'):
				result.append("\\\\");
				break;
			case('\b'):
				result.append("\\b");
				break;
			case('\f'):
				result.append("\\f");
				break;
			default:
				result.append(1, c);
				break;
		}
		i += 1;
	}
	result.append(1, '"');
	return(result);
}
