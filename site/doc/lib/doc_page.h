#pragma once

struct DocPage {
	String title;
	String content;
	StringList sig_lines;
	StringList param_lines;
	StringList see_lines;
};

enum class DocPageKind
{
	function,
	struct_page,
	directive,
	info
};

String doc_default_title(String page)
{
	String page_title = page;
	if(page_title.length() > 1 && page_title[1] == '_')
		nibble(page_title, "_");
	return(page_title);
}

String doc_markdown_inline(String text)
{
	text = trim(text);
	if(text == "")
		return("");
	String html = markdown_to_html(text);
	if(html.length() >= 7 && html.substr(0, 3) == "<p>" && html.substr(html.length() - 4) == "</p>")
		return(html.substr(3, html.length() - 7));
	return(html);
}

String doc_legacy_heading(String section)
{
	if(section == "desc")
		return("");
	if(section == "related")
		return("## PHP & JS Equivalents");
	return("## " + section);
}

bool doc_has_area(String name)
{
	return(file_exists("areas/" + name + ".txt"));
}

bool doc_has_page(String name)
{
	return(file_exists("pages/" + name + ".txt"));
}

DocPageKind doc_page_kind(String page)
{
	if(page.substr(0, 2) == "0_")
		return(DocPageKind::struct_page);
	if(page.substr(0, 2) == "1_")
		return(DocPageKind::directive);
	if(page.substr(0, 2) == "3_")
		return(DocPageKind::info);
	return(DocPageKind::function);
}

String doc_page_kind_badge(DocPageKind kind)
{
	if(kind == DocPageKind::struct_page)
		return("struct");
	if(kind == DocPageKind::directive)
		return("directive");
	if(kind == DocPageKind::info)
		return("info");
	return("");
}

String doc_index_label(String page)
{
	String label = page;
	auto kind = doc_page_kind(page);
	if(kind == DocPageKind::struct_page || kind == DocPageKind::directive || kind == DocPageKind::info)
		nibble(label, "_");
	return(label);
}

DocPage load_doc_page(String page)
{
	DocPage result;
	StringList lines = split(file_get_contents("pages/" + page + ".txt"), "\n");
	String current_section = "";
	bool content_mode = false;
	StringList content_lines;

	for(auto line : lines)
	{
		if(!content_mode && line != "" && line.substr(0, 1) == ":")
		{
			String section = trim(line.substr(1));
			if(section == "title" || section == "sig" || section == "params" || section == "see")
			{
				current_section = section;
				continue;
			}
			if(section == "content")
			{
				content_mode = true;
				current_section = "content";
				continue;
			}

			current_section = "legacy";
			String heading = doc_legacy_heading(section);
			if(heading != "")
			{
				if(content_lines.size() > 0 && content_lines.back() != "")
					content_lines.push_back("");
				content_lines.push_back(heading);
				content_lines.push_back("");
			}
			continue;
		}

		if(current_section == "title")
		{
			if(result.title != "")
				result.title += "\n";
			result.title += line;
		}
		else if(current_section == "sig")
		{
			result.sig_lines.push_back(line);
		}
		else if(current_section == "params")
		{
			result.param_lines.push_back(line);
		}
		else if(current_section == "see")
		{
			if(trim(line) != "")
				result.see_lines.push_back(trim(line));
		}
		else
		{
			content_lines.push_back(line);
		}
	}

	result.content = join(content_lines, "\n");
	result.title = trim(result.title);
	return(result);
}
