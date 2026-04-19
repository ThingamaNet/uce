#pragma once

class StarterUser
{
public:
	Request& context;

	explicit StarterUser(Request& request)
		: context(request)
	{
	}

	static String session_key()
	{
		return("starter_user_id");
	}

	static String normalize_email(String email)
	{
		return(to_lower(trim(email)));
	}

	static String hash_id(String raw)
	{
		raw = normalize_email(raw);
		return(gen_sha1("uce-starter:" + raw).substr(0, 12));
	}

	String root_path()
	{
		return(first(context.cfg.get_by_path("filebase/path").to_string(), "/tmp/uce-starter-data"));
	}

	String dir(String email)
	{
		String bucket = hash_id(email);
		return(path_join(root_path(), "users/" + bucket.substr(0, 2) + "/" + bucket));
	}

	String file_name(String email)
	{
		return(path_join(dir(email), "account.json"));
	}

	static String password_hash(String password, String salt)
	{
		String hash = "uce-starter:" + password + ":" + salt;
		for(s32 i = 0; i < 2048; i++)
			hash = gen_sha1(hash + ":" + std::to_string(i));
		return(hash);
	}

	static DTree read_json_file(String file_name)
	{
		DTree result;
		if(!file_exists(file_name))
			return(result);
		String raw = trim(file_get_contents(file_name));
		if(raw == "")
			return(result);
		return(json_decode(raw));
	}

	static bool write_json_file(String file_name, DTree data)
	{
		mkdir(dirname(file_name));
		return(file_put_contents(file_name, json_encode(data)));
	}

	DTree load(String email)
	{
		DTree user = read_json_file(file_name(email));
		if(user.get_type_name() != "array")
			return(DTree());
		user["id"] = normalize_email(email);
		return(user);
	}

	DTree create(String email, String password)
	{
		DTree result;
		email = normalize_email(email);
		password = trim(password);
		result["message"] = "";
		result["result"].set_bool(false);

		if(email == "" || password == "")
		{
			result["message"] = "email_and_password_required";
			return(result);
		}
		if(email.find("@") == String::npos)
		{
			result["message"] = "invalid_email";
			return(result);
		}

		DTree existing = load(email);
		if(existing.get_type_name() == "array" && existing["email"].to_string() != "")
		{
			result["message"] = "user_exists";
			return(result);
		}

		String salt = gen_sha1(std::to_string((u64)time()) + ":" + std::to_string((u64)(time_precise() * 1000000.0)) + ":" + session_id_create()).substr(0, 24);
		DTree user;
		user["email"] = email;
		user["salt"] = salt;
		user["password_hash"] = password_hash(password, salt);
		user["created"] = std::to_string((u64)time());
		DTree role;
		role = "user";
		user["roles"].push(role);

		write_json_file(file_name(email), user);
		user["id"] = email;
		result["result"].set_bool(true);
		result["id"] = email;
		result["profile"] = user;
		return(result);
	}

	DTree sign_in(String email, String password)
	{
		DTree result;
		result["result"].set_bool(false);
		email = normalize_email(email);
		password = trim(password);

		DTree user = load(email);
		if(user.get_type_name() != "array" || user["email"].to_string() == "")
		{
			result["message"] = "no_such_user";
			return(result);
		}
		if(user["password_hash"].to_string() == "")
		{
			result["message"] = "no_password_set";
			return(result);
		}

		String expected = password_hash(password, user["salt"].to_string());
		if(expected != user["password_hash"].to_string())
		{
			result["message"] = "invalid_password";
			return(result);
		}

		session_start();
		context.session[session_key()] = email;
		context.var["starter"]["current_user"] = user;
		result["result"].set_bool(true);
		result["profile"] = user;
		return(result);
	}

	bool is_signed_in()
	{
		if(context.var["starter"]["current_user"]["email"].to_string() != "")
			return(true);
		String user_id = context.session[session_key()];
		if(user_id == "")
			return(false);
		DTree user = load(user_id);
		if(user["email"].to_string() == "")
		{
			context.session.erase(session_key());
			return(false);
		}
		context.var["starter"]["current_user"] = user;
		return(true);
	}

	DTree current()
	{
		if(is_signed_in())
			return(context.var["starter"]["current_user"]);
		return(DTree());
	}

	void logout()
	{
		context.session.erase(session_key());
		context.var["starter"]["current_user"].clear();
	}
};
