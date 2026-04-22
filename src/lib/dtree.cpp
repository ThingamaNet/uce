
#include <cctype>
#include <cmath>
#include <limits>

namespace {

template <typename TreePtr>
TreePtr dtree_resolve_reference(TreePtr tree)
{
	u32 depth = 0;
	while(tree && tree->type == 'R' && depth < 16)
	{
		TreePtr target = reinterpret_cast<TreePtr>(tree->_ptr);
		if(target == 0 || target == tree)
			break;
		tree = target;
		depth += 1;
	}
	return(tree);
}

bool dtree_key_is_index(String key, s64 expected_index = -1)
{
	if(key == "")
		return(false);
	for(auto c : key)
	{
		if(!isdigit(c))
			return(false);
	}
	if(expected_index >= 0)
		return(key == std::to_string(expected_index));
	return(true);
}

String dtree_trim(String raw)
{
	if(raw == "")
		return(raw);

	size_t start = 0;
	while(start < raw.length() && isspace(raw[start]))
		start += 1;

	size_t end = raw.length();
	while(end > start && isspace(raw[end - 1]))
		end -= 1;

	return(raw.substr(start, end - start));
}

String dtree_lower(String raw)
{
	for(auto& c : raw)
		c = (char)tolower(c);
	return(raw);
}

bool dtree_string_to_bool_value(String raw, bool& value_out)
{
	raw = dtree_lower(dtree_trim(raw));
	if(raw == "")
		return(false);
	if(raw == "1" || raw == "true" || raw == "(true)" || raw == "yes" || raw == "on")
	{
		value_out = true;
		return(true);
	}
	if(raw == "0" || raw == "false" || raw == "(false)" || raw == "no" || raw == "off" || raw == "null")
	{
		value_out = false;
		return(true);
	}
	return(false);
}

bool dtree_string_to_f64_value(String raw, f64& value_out)
{
	raw = dtree_trim(raw);
	if(raw == "")
		return(false);

	bool bool_value = false;
	if(dtree_string_to_bool_value(raw, bool_value))
	{
		value_out = (bool_value ? 1.0 : 0.0);
		return(true);
	}

	char* end = 0;
	value_out = strtod(raw.c_str(), &end);
	if(end == raw.c_str())
		return(false);
	while(end && *end != 0)
	{
		if(!isspace(*end))
			return(false);
		end += 1;
	}
	return(std::isfinite(value_out));
}

const DTree* dtree_scalar_map_value(const DTree& tree)
{
	if(tree.type != 'M' || tree._map.size() != 1)
		return(0);
	auto it = tree._map.begin();
	if(it == tree._map.end())
		return(0);
	return(&it->second.deref());
}

f64 dtree_clamp_to_f64_range(long double value)
{
	if(value > std::numeric_limits<f64>::max())
		return(std::numeric_limits<f64>::max());
	if(value < -std::numeric_limits<f64>::max())
		return(-std::numeric_limits<f64>::max());
	return((f64)value);
}

s64 dtree_clamp_to_s64_range(long double value)
{
	if(value > (long double)std::numeric_limits<s64>::max())
		return(std::numeric_limits<s64>::max());
	if(value < (long double)std::numeric_limits<s64>::min())
		return(std::numeric_limits<s64>::min());
	return((s64)value);
}

u64 dtree_clamp_to_u64_range(long double value)
{
	if(value <= 0)
		return(0);
	if(value > (long double)std::numeric_limits<u64>::max())
		return(std::numeric_limits<u64>::max());
	return((u64)value);
}

}

void DTree::each(std::function <void (DTree t, String key)> f)
{
	const DTree& target = deref();
	switch(target.type)
	{
		case('M'):
			for (auto it = target._map.begin(); it != target._map.end(); ++it)
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
	return(deref().type == 'M');
}

bool DTree::is_list() const
{
	const DTree& target = deref();
	if(target.type != 'M')
		return(false);
	if(target._map.size() == 0)
		return(target._list_mode);
	s64 expected_index = 0;
	for(const auto& entry : target._map)
	{
		if(!dtree_key_is_index(entry.first, expected_index))
			return(false);
		expected_index += 1;
	}
	return(true);
}

String DTree::to_string()
{
	const DTree& target = deref();
	switch(target.type)
	{
		case('S'):
			return(target._String);
			break;
		case('F'):
			return(std::to_string(target._float));
			break;
		case('B'):
			return(target._bool ? "(true)" : "(false)");
			break;
		case('M'):
			return("");
			break;
		case('P'):
			return(std::to_string((u64)target._ptr));
			break;
		case('R'):
			return("");
			break;
	}
	return("");
}

s64 DTree::to_s64()
{
	const DTree& target = deref();
	switch(target.type)
	{
		case('S'):
		{
			f64 value = 0;
			if(!dtree_string_to_f64_value(target._String, value))
				return(0);
			return(dtree_clamp_to_s64_range((long double)value));
		}
		case('F'):
			return(dtree_clamp_to_s64_range((long double)target._float));
		case('B'):
			return(target._bool ? 1 : 0);
		case('M'):
		{
			const DTree* item = dtree_scalar_map_value(target);
			if(item)
				return(const_cast<DTree*>(item)->to_s64());
			return(0);
		}
		case('P'):
			return(dtree_clamp_to_s64_range((long double)(u64)target._ptr));
		case('R'):
			return(0);
	}
	return(0);
}

u64 DTree::to_u64()
{
	const DTree& target = deref();
	switch(target.type)
	{
		case('S'):
		{
			f64 value = 0;
			if(!dtree_string_to_f64_value(target._String, value))
				return(0);
			return(dtree_clamp_to_u64_range((long double)value));
		}
		case('F'):
			return(dtree_clamp_to_u64_range((long double)target._float));
		case('B'):
			return(target._bool ? 1 : 0);
		case('M'):
		{
			const DTree* item = dtree_scalar_map_value(target);
			if(item)
				return(const_cast<DTree*>(item)->to_u64());
			return(0);
		}
		case('P'):
			return((u64)target._ptr);
		case('R'):
			return(0);
	}
	return(0);
}

f64 DTree::to_f64()
{
	const DTree& target = deref();
	switch(target.type)
	{
		case('S'):
		{
			f64 value = 0;
			if(!dtree_string_to_f64_value(target._String, value))
				return(0);
			return(value);
		}
		case('F'):
			return(target._float);
		case('B'):
			return(target._bool ? 1.0 : 0.0);
		case('M'):
		{
			const DTree* item = dtree_scalar_map_value(target);
			if(item)
				return(const_cast<DTree*>(item)->to_f64());
			return(0);
		}
		case('P'):
			return(dtree_clamp_to_f64_range((long double)(u64)target._ptr));
		case('R'):
			return(0);
	}
	return(0);
}

bool DTree::to_bool()
{
	const DTree& target = deref();
	switch(target.type)
	{
		case('S'):
		{
			bool value = false;
			if(dtree_string_to_bool_value(target._String, value))
				return(value);
			f64 numeric_value = 0;
			if(dtree_string_to_f64_value(target._String, numeric_value))
				return(numeric_value != 0);
			return(dtree_trim(target._String) != "");
		}
		case('F'):
			return(target._float != 0);
		case('B'):
			return(target._bool);
		case('M'):
		{
			const DTree* item = dtree_scalar_map_value(target);
			if(item)
				return(const_cast<DTree*>(item)->to_bool());
			return(target._map.size() > 0);
		}
		case('P'):
			return(target._ptr != 0);
		case('R'):
			return(false);
	}
	return(false);
}

StringMap DTree::to_stringmap()
{
	const DTree& target = deref();
	StringMap result;
	switch(target.type)
	{
		case('M'):
			for(const auto& entry : target._map)
				result[entry.first] = const_cast<DTree&>(entry.second.deref()).to_string();
			break;
		case('S'):
			if(dtree_trim(target._String) != "")
				result["value"] = target._String;
			break;
		case('F'):
		case('B'):
		case('P'):
			result["value"] = const_cast<DTree&>(target).to_string();
			break;
		case('R'):
			break;
	}
	return(result);
}

String DTree::to_json(char quote_char)
{
	const DTree& target = deref();
	switch(target.type)
	{
		case('S'):
			return(json_escape(target._String, quote_char));
			break;
		case('F'):
			return(std::to_string(target._float));
			break;
		case('B'):
			return(target._bool ? "true" : "false");
			break;
		case('M'):
			return("\"(array)\"");
			break;
		case('P'):
			return("\"(pointer)\"");
			break;
		case('R'):
			return("\"(reference)\"");
			break;
	}
	return("\"(unknown)\"");
}

String DTree::get_type_name()
{
	const DTree& target = deref();
	switch(target.type)
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
		case('R'):
			return("reference");
			break;
	}
	return("unknown");
}

DTree DTree::get_by_path(String path, String delim)
{
	const DTree* current = &deref();
	if(path == "")
		return(*current);
	size_t start = 0;
	while(start <= path.length())
	{
		size_t end = path.find(delim, start);
		String segment;
		if(end == String::npos)
			segment = path.substr(start);
		else
			segment = path.substr(start, end - start);
		if(segment == "")
		{
			if(end == String::npos)
				break;
			start = end + delim.length();
			continue;
		}
		current = &current->deref();
		if(current->type != 'M')
			return(DTree());
		auto it = current->_map.find(segment);
		if(it == current->_map.end())
			return(DTree());
		current = &it->second;
		if(end == String::npos)
			break;
		start = end + delim.length();
	}
	return(current->deref());
}

bool DTree::is_reference()
{
	return(type == 'R');
}

DTree* DTree::reference_target()
{
	if(type != 'R')
		return(0);
	DTree* target = dtree_resolve_reference(this);
	if(target == 0 || target == this || target->type == 'R')
		return(0);
	return(target);
}

const DTree* DTree::reference_target() const
{
	if(type != 'R')
		return(0);
	const DTree* target = dtree_resolve_reference(this);
	if(target == 0 || target == this || target->type == 'R')
		return(0);
	return(target);
}

DTree& DTree::deref()
{
	DTree* target = dtree_resolve_reference(this);
	if(target == 0)
		return(*this);
	return(*target);
}

const DTree& DTree::deref() const
{
	const DTree* target = dtree_resolve_reference(this);
	if(target == 0)
		return(*this);
	return(*target);
}

void DTree::set_type(char t)
{
	DTree* target = reference_target();
	if(target)
	{
		target->set_type(t);
		return;
	}
	if(type != t)
	{
		type = t;
		switch(type)
		{
			case('M'):
				_map.clear();
				_array_index = 0;
				_list_mode = false;
				break;
		}
	}
}

void DTree::set(String s)
{
	DTree* target = reference_target();
	if(target)
	{
		target->set(s);
		return;
	}
	set_type('S');
	_String = s;
	_list_mode = false;
}

void DTree::set(void* p)
{
	DTree* target = reference_target();
	if(target)
	{
		target->set(p);
		return;
	}
	set_type('P');
	_ptr = p;
	_list_mode = false;
}

void DTree::set(f64 f)
{
	DTree* target = reference_target();
	if(target)
	{
		target->set(f);
		return;
	}
	set_type('F');
	_float = f;
	_list_mode = false;
}

void DTree::set_bool(bool b)
{
	DTree* target = reference_target();
	if(target)
	{
		target->set_bool(b);
		return;
	}
	set_type('B');
	_bool = b;
	_list_mode = false;
}

void DTree::set(DTree source)
{
	DTree* target = reference_target();
	if(target)
	{
		target->set(source);
		return;
	}
	set_type(source.type);
	switch(type)
	{
		case('S'):
			_String = source._String;
			_list_mode = false;
			break;
		case('F'):
			_float = source._float;
			_list_mode = false;
			break;
		case('B'):
			_bool = source._bool;
			_list_mode = false;
			break;
		case('M'):
			_map = source._map;
			_array_index = source._array_index;
			_list_mode = source._list_mode;
			break;
		case('P'):
			_ptr = source._ptr;
			_list_mode = false;
			break;
		case('R'):
			_ptr = source._ptr;
			_list_mode = false;
			break;
	}
}

void DTree::set(StringMap source)
{
	DTree* target = reference_target();
	if(target)
	{
		target->set(source);
		return;
	}
	set_type('M');
	_map.clear();
	_array_index = 0;
	_list_mode = false;
	for (auto it = source.begin(); it != source.end(); ++it)
	{
		_map[it->first] = it->second;
	}
}

void DTree::set_array()
{
	DTree* target = reference_target();
	if(target)
	{
		target->set_array();
		return;
	}
	type = 'M';
	_map.clear();
	_array_index = 0;
	_list_mode = true;
}

void DTree::set_reference(DTree* target)
{
	type = 'R';
	_ptr = target;
}

bool DTree::has(String s) const
{
	const DTree& target = deref();
	if(target.type != 'M')
		return(false);
	return(target._map.find(s) != target._map.end());
}

DTree* DTree::key(String s)
{
	DTree* target = reference_target();
	if(target)
		return(target->key(s));
	if(type != 'M')
		return(0);
	auto it = _map.find(s);
	if(it == _map.end())
		return(0);
	return(&it->second);
}

const DTree* DTree::key(String s) const
{
	const DTree& target = deref();
	if(target.type != 'M')
		return(0);
	auto it = target._map.find(s);
	if(it == target._map.end())
		return(0);
	return(&it->second);
}

DTree* DTree::get_or_create(String s)
{
	DTree* target = reference_target();
	if(target)
		return(target->get_or_create(s));
	set_type('M');
	if(_list_mode && !dtree_key_is_index(s))
		_list_mode = false;
	return(&_map[s]);
}

DTree& DTree::operator [] (String s) {
	DTree* target = reference_target();
	if(target)
		return((*target)[s]);
	return(*get_or_create(s));
}

void DTree::operator = (String v) { set(v); }
void DTree::operator = (f64 v) { set(v); }
void DTree::operator = (void* v) { set(v); }
void DTree::operator = (DTree v) { set(v); }
void DTree::operator = (StringMap v) { set(v); }

void DTree::push(DTree& child)
{
	DTree* target = reference_target();
	if(target)
	{
		target->push(child);
		return;
	}
	set_type('M');
	if(_map.size() == 0)
	{
		_list_mode = true;
		_array_index = 0;
	}
	else
	{
		if(is_list())
		{
			_list_mode = true;
			_array_index = _map.size();
		}
		else
		{
			_list_mode = false;
			while(_map.find(std::to_string(_array_index)) != _map.end())
				_array_index += 1;
		}
	}
	_map[std::to_string(_array_index)] = child;
	_array_index += 1;
}

DTree DTree::pop()
{
	DTree* target = reference_target();
	if(target)
		return(target->pop());
	set_type('M');
	auto last = _map.rbegin();
	DTree result = last->second;
	_map.erase(last->first);
	if(_list_mode)
		_array_index = _map.size();
	return(result);
}

void DTree::remove(String s)
{
	DTree* target = reference_target();
	if(target)
	{
		target->remove(s);
		return;
	}
	set_type('M');
	_map.erase(s);
	if(_map.size() == 0)
		_array_index = 0;
}

void DTree::clear()
{
	DTree* target = reference_target();
	if(target)
	{
		target->clear();
		return;
	}
	set_type('M');
	_map.clear();
	_array_index = 0;
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

String json_escape(String s, char quote_char)
{
	//return(String("\"")+s+"\"");
	String result;
	u32 i = 0;
	result.append(1, quote_char);
	while(i < s.length())
	{
		char c = s[i];
		if(c == quote_char)
		{
			result.append(1, '\\');
			result.append(1, quote_char);
		}
		else switch(c)
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
	result.append(1, quote_char);
	return(result);
}
