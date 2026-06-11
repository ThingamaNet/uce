#pragma once

struct SQLite {

	void* connection = 0;
	s32 error_code = 0;
	u32 affected_rows = 0;
	u64 insert_id = 0;
	String statement_info = "";
	String path = "";
	bool request_cleanup_delete = false;
	bool worker_cache = false;

	bool connect(String path);
	void disconnect();
	String error();
	DTree query(String q);
	DTree query(String q, const StringMap& params);

private:
	void set_error(s32 code, String info = "");
	bool apply_default_pragmas();
	bool bind_params(void* statement, const StringMap& params);
	DTree collect_rows(void* statement);
};

SQLite* sqlite_connect(String path);
void sqlite_disconnect(SQLite* db);
String sqlite_error(SQLite* db);
DTree sqlite_query(SQLite* db, String q);
DTree sqlite_query(SQLite* db, String q, const StringMap& params);
u64 sqlite_insert_id(SQLite* db);
u32 sqlite_affected_rows(SQLite* db);
void cleanup_sqlite_connections();
