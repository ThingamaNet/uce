
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

String DTree::to_json()
{
	const DTree& target = deref();
	switch(target.type)
	{
		case('S'):
			return(json_escape(target._String));
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
		case('R'):
			_ptr = source._ptr;
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
	for (auto it = source.begin(); it != source.end(); ++it)
	{
		_map[it->first] = it->second;
	}
}

void DTree::set_reference(DTree* target)
{
	type = 'R';
	_ptr = target;
}

DTree* DTree::key(String s)
{
	DTree* target = reference_target();
	if(target)
		return(target->key(s));
	set_type('M');
	return(&_map[s]);
}

DTree& DTree::operator [] (String s) {
	DTree* target = reference_target();
	if(target)
		return((*target)[s]);
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
	DTree* target = reference_target();
	if(target)
	{
		target->push(child);
		return;
	}
	set_type('M');
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
