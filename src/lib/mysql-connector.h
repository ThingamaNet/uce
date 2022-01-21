struct MySQLFieldInfo {
	String name;
	String table;
	String db;
	u64 length;
	String def;
	u64 max_length;
	u64 flags;
	u32 type;
};

struct MySQL {

	void* connection = 0;
	u32 _preload_next_error_code = 0;
	u32 affected_rows = 0;
	u32 field_count = 0;
	u32 row_count = 0;
	u64 insert_id = 0;
	String statement_info = ""; //

	std::vector<MySQLFieldInfo> field_info;

	bool connect(String host = "localhost", String username = "root", String password = "");
	void disconnect();
	String error();
	String escape(String raw, char quote_char = '\'');
	String parse_query_parameters(String query, StringMap m);
	DTree query(String q);
	DTree query(String q, StringMap params);
	DTree get_pending_result();

};

MySQL* mysql_connect(String host = "localhost", String username = "root", String password = "")
{
	MySQL* m = new MySQL();
	m->connect(host, username, password);
	return(m);
}

void mysql_disconnect(MySQL* m)
{
	m->disconnect();
}

String mysql_error(MySQL* m)
{
	return(m->error());
}

String mysql_escape(String raw, char quote_char);

DTree mysql_query(MySQL* m, String q)
{
	return(m->query(q));
}

DTree mysql_query(MySQL* m, String q, StringMap params)
{
	return(m->query(q, params));
}

u64 mysql_insert_id(MySQL* m)
{
	return(m->insert_id);
}
