#include "compiler-parser.h"

#include "compiler.h"

namespace {

const char* UCE_NAMED_RENDER_SYMBOL = "__uce_render";
const char* UCE_NAMED_COMPONENT_SYMBOL = "__uce_component";

struct CompilerCodeState
{
	char quote_char = '\0';
	bool inside_quote = false;
	bool inside_line_comment = false;
	bool inside_block_comment = false;
};

bool compiler_code_state_is_neutral(const CompilerCodeState& state)
{
	return(!state.inside_quote && !state.inside_line_comment && !state.inside_block_comment);
}

void compiler_code_state_consume(CompilerCodeState& state, String& buffer, const String& content, u32& i)
{
	char c = content[i];
	char c1 = (i + 1 < content.length()) ? content[i + 1] : '\0';

	buffer.append(1, c);

	if(state.inside_quote)
	{
		if(state.quote_char == c && (i == 0 || content[i-1] != '\\'))
			state.inside_quote = false;
		return;
	}

	if(state.inside_line_comment)
	{
		if(c == '\n')
			state.inside_line_comment = false;
		return;
	}

	if(state.inside_block_comment)
	{
		if(c == '*' && c1 == '/')
		{
			buffer.append(1, c1);
			state.inside_block_comment = false;
			i += 1;
		}
		return;
	}

	if(c == '/' && c1 == '/')
	{
		buffer.append(1, c1);
		state.inside_line_comment = true;
		i += 1;
		return;
	}

	if(c == '/' && c1 == '*')
	{
		buffer.append(1, c1);
		state.inside_block_comment = true;
		i += 1;
		return;
	}

	if(c == '"' || c == '\'')
	{
		state.inside_quote = true;
		state.quote_char = c;
	}
}

String compiler_capture_markup_literal(const String& content, u32& i)
{
	String buffer = "";
	u32 depth = 0;
	bool inside_code = false;
	CompilerCodeState code_state;
	u32 j = i + 2;

	while(j < content.length())
	{
		char c = content[j];
		char c1 = (j + 1 < content.length()) ? content[j + 1] : '\0';
		char c2 = (j + 2 < content.length()) ? content[j + 2] : '\0';

		if(inside_code)
		{
			if(compiler_code_state_is_neutral(code_state) && c == '?' && c1 == '>')
			{
				buffer.append(1, c);
				buffer.append(1, c1);
				inside_code = false;
				j += 2;
				continue;
			}

			compiler_code_state_consume(code_state, buffer, content, j);
			j += 1;
			continue;
		}

		if(c == '<' && c1 == '?')
		{
			inside_code = true;
			code_state = CompilerCodeState();
			buffer.append(1, c);
			buffer.append(1, c1);
			j += 2;
			continue;
		}

		if(c == '<' && c1 == '/' && c2 == '>')
		{
			if(depth > 0)
			{
				depth -= 1;
				buffer.append("</>");
				j += 3;
				continue;
			}

			i = j + 2;
			return(buffer);
		}

		if(c == '<' && c1 == '>')
		{
			depth += 1;
			buffer.append("<>");
			j += 2;
			continue;
		}

		buffer.append(1, c);
		j += 1;
	}

	i = content.length();
	return(buffer);
}

String compiler_process_text_literal(Request* context, SharedUnit* su, String content)
{
	String parsed_content;
	String html_output_start = "print(R\"(";
	String html_output_end = ")\");";
	String code_buffer = "";
	CompilerCodeState code_state;
	bool inside_code = false;
	bool is_field = false;
	bool escape_field = false;

	for(u32 i = 0; i < content.length(); i++)
	{
		char c = content[i];
		char c1 = (i + 1 < content.length()) ? content[i + 1] : '\0';
		char c2 = (i + 2 < content.length()) ? content[i + 2] : '\0';

		if(!inside_code)
		{
			if(c == '<' && c1 == '?')
			{
				inside_code = true;
				code_buffer = "";
				code_state = CompilerCodeState();
				if(c2 == '=')
				{
					is_field = true;
					escape_field = true;
					i += 2;
				}
				else if(c2 == ':')
				{
					is_field = true;
					escape_field = false;
					i += 2;
				}
				else
				{
					is_field = false;
					escape_field = false;
					i += 1;
				}
				continue;
			}

			parsed_content.append(1, c);
			continue;
		}

		if(compiler_code_state_is_neutral(code_state) && c == '?' && c1 == '>')
		{
			inside_code = false;
			i += 1;
			if(is_field)
			{
				if(escape_field)
				{
					parsed_content.append(
						html_output_end +
						"print(html_escape( " +
						code_buffer +
						" )); " +
						html_output_start
					);
				}
				else
				{
					parsed_content.append(
						html_output_end +
						"print( " +
						code_buffer +
						" ); " +
						html_output_start
					);
				}
			}
			else
			{
				parsed_content.append(html_output_end + code_buffer + html_output_start);
			}
			continue;
		}

		if(compiler_code_state_is_neutral(code_state) && c == '<' && c1 == '>')
		{
			String nested_markup = compiler_capture_markup_literal(content, i);
			code_buffer.append(compiler_process_text_literal(context, su, nested_markup));
			continue;
		}

		compiler_code_state_consume(code_state, code_buffer, content, i);
	}

	return(html_output_start + parsed_content + html_output_end);
}

String compiler_rewrite_named_render_syntax(String content)
{
	String result = "";
	String current_line = "";

	auto flush_line = [&]() {
		if(current_line.length() == 0)
			return;

		String line = current_line;
		String line_break = "";
		if(line.length() > 0 && line.back() == '\n')
		{
			line_break = "\n";
			line.pop_back();
		}

		u32 indent_length = 0;
		while(indent_length < line.length() && isspace(line[indent_length]))
			indent_length += 1;

		String indent = line.substr(0, indent_length);
		String trimmed = trim(line);
		if(trimmed.rfind("RENDER:", 0) == 0)
		{
			String signature = trimmed.substr(7);
			auto open_paren_pos = signature.find("(");
			if(open_paren_pos != String::npos)
			{
				String render_name = trim(signature.substr(0, open_paren_pos));
				String render_signature = signature.substr(open_paren_pos);
				if(render_name != "")
					line = indent + "EXPORT void " + String(UCE_NAMED_RENDER_SYMBOL) + "_" + safe_name(render_name) + render_signature;
			}
		}
		else if(trimmed.rfind("COMPONENT:", 0) == 0)
		{
			String signature = trimmed.substr(10);
			auto open_paren_pos = signature.find("(");
			if(open_paren_pos != String::npos)
			{
				String render_name = trim(signature.substr(0, open_paren_pos));
				String render_signature = signature.substr(open_paren_pos);
				if(render_name != "")
					line = indent + "EXPORT void " + String(UCE_NAMED_COMPONENT_SYMBOL) + "_" + safe_name(render_name) + render_signature;
			}
		}

		result += line + line_break;
		current_line = "";
	};

	for(auto c : content)
	{
		current_line.append(1, c);
		if(c == '\n')
			flush_line();
	}
	flush_line();

	return(result);
}

String compiler_preprocess_shared_unit_char_wise(Request* context, SharedUnit* su, String content)
{
	String parsed_content =
		("#include \"")+context->server->config["COMPILER_SYS_PATH"] +"/src/lib/uce_lib.h\" \n"+
		file_get_contents(
			context->server->config["COMPILER_SYS_PATH"] + "/" + context->server->config["SETUP_TEMPLATE"]
		)+
		"#line 1\n";
	CompilerCodeState code_state;
	String current_line = "";

	for(u32 i = 0; i < content.length(); i++)
	{
		char c = content[i];
		char c1 = (i + 1 < content.length()) ? content[i + 1] : '\0';
		current_line.append(1, c);

		if(compiler_code_state_is_neutral(code_state) && c == '<' && c1 == '>')
		{
			String markup_literal = compiler_capture_markup_literal(content, i);
			parsed_content.append(compiler_process_text_literal(context, su, markup_literal));
			if(i < content.length() && content[i] == '\n')
				current_line = "\n";
			continue;
		}

		compiler_code_state_consume(code_state, parsed_content, content, i);

		if(c == 10 && current_line.substr(0, 6) == "#load ")
		{
			parsed_content.resize(parsed_content.length() - current_line.length());
			nibble(current_line, "\"");
			String unit_name = nibble(current_line, "\"");
			SharedUnit* sub_su = compiler_load_shared_unit(context, unit_name, su->src_path, true);
			if(sub_su)
				parsed_content.append("#include \"" + sub_su->bin_path + "/" + sub_su->pre_file_name + "\"\n");
		}
		else
		{
			String trimmed_line = trim(current_line);
			if(c == 10 && trimmed_line.length() > 7 && trimmed_line.substr(0, 6) == "EXPORT" && isspace(trimmed_line[6]))
			{
				current_line = "";
				auto end_declaration_pos = content.find("{", i);
				if(end_declaration_pos != std::string::npos)
				{
					parsed_content.append(1, '\n');
					String declaration = trim(content.substr(i, end_declaration_pos - i));
					su->api_declarations.push_back(declaration+";\n");
				}
			}
		}

		if(c == 10)
			current_line = "";
	}

	return(parsed_content);
}

}

String compiler_preprocess_source(Request* context, SharedUnit* su, String content)
{
	content = compiler_rewrite_named_render_syntax(content);
	return(compiler_preprocess_shared_unit_char_wise(context, su, content));
}