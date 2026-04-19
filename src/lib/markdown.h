#pragma once

DTree markdown_to_ast(String src);
DTree markdown_to_ast(String src, DTree options);
String markdown_to_html(String src);
String markdown_to_html(String src, DTree options);
