#include "../3rdparty/mysql/mysql.h"
#include <stdio.h>
#include <stdlib.h>
#include "mysql-connector.h"

bool MySQL::connect(String host, String username, String password)
{
	//switch_to_system_alloc();
	connection = mysql_init(NULL);
	if (connection == NULL)
	{
		auto e = mysql_error((MYSQL*)connection);
		fprintf(stderr, "%s\n", e);
		//switch_to_arena(context->mem);
		statement_info.assign(e);
		return(false);
	}

	if (mysql_real_connect((MYSQL*)connection, host.c_str(), username.c_str(), password.c_str(),
	  NULL, 0, NULL, 0) == NULL)
	{
		auto e = mysql_error((MYSQL*)connection);
		fprintf(stderr, "%s\n", e);
		mysql_close((MYSQL*)connection);
		//switch_to_arena(context->mem);
		statement_info.assign(e);
		return(false);
	}

	/*
	if (mysql_query(con, "CREATE DATABASE testdb"))
	{
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		exit(1);
	}
	*/
	//switch_to_arena(context->mem);
	statement_info = String("connected");
	context->resources.mysql_connections.push_back(connection);
	return(true);
}

String MySQL::escape(String raw, char quote_char)
{
	return(mysql_escape(raw, quote_char));
}

String mysql_escape(String raw, char quote_char)
{
	String result;
	if(quote_char > 0)
		result.append(1, quote_char);

	for(u32 i = 0; i < raw.length(); i++)
	{
		char c = raw[i];
		switch(c)
		{
			case('\n'):
				result.append("\\n");
				break;
			case('\r'):
				result.append("\\r");
				break;
			case('\t'):
				result.append("\\t");
				break;
			case('\\'):
			case('\''):
			case('"'):
				result.append(1, '\\');
				result.append(1, c);
				break;
			default:
				result.append(1, c);
				break;
		}
	}

	if(quote_char > 0)
		result.append(1, quote_char);
	return(result);
}

DTree field_to_dtree_node(char* data_ptr, MySQLFieldInfo field_info, u32 len)
{
	DTree result;
	switch(field_info.type)
	{
		case(MYSQL_TYPE_TINY):
			result.set(atoll(data_ptr));
			break;
		case(MYSQL_TYPE_SHORT):
			result.set(atoll(data_ptr));
			break;
		case(MYSQL_TYPE_LONG):
			result.set(atoll(data_ptr));
			break;
		case(MYSQL_TYPE_INT24):
			result.set(atoll(data_ptr));
			break;
		case(MYSQL_TYPE_LONGLONG):
			result.set(atoll(data_ptr));
			break;
		case(MYSQL_TYPE_FLOAT):
			result.set(atof(data_ptr));
			break;
		case(MYSQL_TYPE_DOUBLE):
			result.set(atof(data_ptr));
			break;
		case(MYSQL_TYPE_NULL):
			break;
		default:
			String s;
			s.assign(data_ptr);
			result.set(s);
			break;
	}
	return(result);
}

DTree MySQL::get_pending_result()
{
	DTree result_data;
	// based on: https://dev.mysql.com/doc/c-api/5.7/en/mysql-field-count.html
	MYSQL_RES *result;
	result = mysql_store_result((MYSQL*)connection);
	insert_id = mysql_insert_id((MYSQL*)connection);
	//statement_info.assign(mysql_info((MYSQL*)connection));
    if (result)  // there are rows
    {
        field_count = mysql_num_fields(result);
        row_count = mysql_num_rows(result);

		field_info.clear();
		unsigned int i;
		MYSQL_FIELD *fields;
		fields = mysql_fetch_fields(result);
		for(i = 0; i < field_count; i++)
		{
			MySQLFieldInfo fi;
			if(fields[i].name) fi.name.assign(fields[i].name);
			if(fields[i].table) fi.table.assign(fields[i].table);
			if(fields[i].db) fi.db.assign(fields[i].db);
			fi.length = (fields[i].length);
			if(fields[i].def) fi.def.assign(fields[i].def);
			fi.max_length = (fields[i].max_length);
			fi.flags = (fields[i].flags);
			fi.type = (fields[i].type);
			field_info.push_back(fi);
		}

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)))
		{
			DTree row_data;
			auto lengths = mysql_fetch_lengths(result);
			for(i = 0; i < field_count; i++)
			{
				row_data[field_info[i].name] = field_to_dtree_node(row[i], field_info[i], lengths[i]);
			}
			result_data.push(row_data);
		}

        mysql_free_result(result);
    }
    else  // mysql_store_result() returned nothing; should it have?
    {
        if(mysql_field_count((MYSQL*)connection) == 0)
        {
            // query does not return data
            // (it was not a SELECT)
            affected_rows = mysql_affected_rows((MYSQL*)connection);
        }
        else // mysql_store_result() should have returned data
        {
			// error
        }
    }
    return(result_data);
}

DTree MySQL::query(String q)
{
	_preload_next_error_code = mysql_query((MYSQL*)connection, q.c_str());
	DTree result;
	if(_preload_next_error_code == 0)
		result = get_pending_result();
	return(result);
}

DTree MySQL::query(String q, StringMap params)
{
	return(query(
		parse_query_parameters(q, params).c_str()
	));
}

String MySQL::parse_query_parameters(String query, StringMap map)
 {
	String result;
	query.append(1, ' ');

	u8 mode = 0;
	char quote;
	String identifier;
	for(u32 i = 0; i < query.length(); i++)
	{
		char c = query[i];
		if(mode == 0) // normal, unquoted mode
		{
			if(c == ':')
			{
				mode = 1;
				identifier = "";
			}
			else if(c == '"' || c == '\'')
			{
				result.append(1, c);
				mode = 2;
				quote = c;
			}
			else
			{
				result.append(1, c);
			}
		}
		else if(mode == 1) // identifier mode
		{
			if(isalnum(c))
			{
				identifier.append(1, c);
			}
			else
			{
				result.append(escape(map[identifier]));
				result.append(1, c);
				mode = 0;
			}
		}
		else if(mode == 2) // quoted mode
		{
			if(c == quote)
				mode = 0;
			result.append(1, c);
		}
	}

	return(result);
 }

void MySQL::disconnect()
{
	if(connection)
		mysql_close((MYSQL*)connection);
	connection = NULL;
}

String MySQL::error()
{
	if(_preload_next_error_code)
	{
		String p = "Unknown error";
		switch(_preload_next_error_code)
		{
			case(CR_COMMANDS_OUT_OF_SYNC):
				p = "Commands out of sync";
				break;
			case(CR_SERVER_GONE_ERROR):
				p = "Server connection error";
				break;
			case(CR_SERVER_LOST):
				p = "Server hung up";
				break;
			case(CR_OUT_OF_MEMORY):
				p = "Out of memory";
				break;
			default:
			case(CR_UNKNOWN_ERROR):
				p = "Unknown server error";
				break;
		}
		_preload_next_error_code = 0;
		return(p);
	}
	const char* res = mysql_error((MYSQL*)connection);
	if(res)
	{
		return(String(res));
	}
	else
	{
		return("");
	}
}

void cleanup_mysql_connections()
{
	//switch_to_system_alloc();
	for(auto& con : context->resources.mysql_connections)
		mysql_close((MYSQL*)con);
	//switch_to_arena(context->mem);
}
