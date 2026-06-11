def register(registry):
	pages = [
		("doc index", "/doc/index.uce", "<html>"),
		("doc singlepage", "/doc/singlepage.uce", "<html>"),
		("doc component page", "/doc/index.uce?p=component", "component()"),
		("doc regex page", "/doc/index.uce?p=regex_search", "regex_search"),
		("doc xml page", "/doc/index.uce?p=xml_encode", "xml_encode"),
		("doc yaml page", "/doc/index.uce?p=yaml_encode", "yaml_encode"),
		("doc relative time page", "/doc/index.uce?p=time_format_relative", "time_format_relative"),
	]

	starter_pages = [
		("starter home", "/examples/uce-starter/", 200, "Stunning Apps"),
		("starter dashboard", "/examples/uce-starter/?dashboard", 200, "Dashboard"),
		("starter workspace nested route", "/examples/uce-starter/?workspace/projects", 200, "Workspace"),
		("starter ajax section", "/examples/uce-starter/?page2-section1", 200, "UCE starter AJAX fragment response"),
		("starter route traversal blocked", "/examples/uce-starter/?../../../demo/index", 404, "The requested page does not exist."),
	]

	for name, path, needle in pages:
		def make_case(page_path=path, expected_text=needle):
			def run(context):
				response = context.expect_status(page_path, 200)
				context.expect_body_contains(response, expected_text)
				return "HTTP 200 with expected body marker for %s" % page_path

			return run

		registry.case(name, make_case(), tags=["http", "smoke", "uce", "public"])

	for name, path, status, needle in starter_pages:
		def make_starter_case(page_path=path, expected_status=status, expected_text=needle):
			def run(context):
				response = context.expect_status(page_path, expected_status)
				context.expect_body_contains(response, expected_text)
				return "starter route %s returned HTTP %s with expected marker" % (page_path, expected_status)

			return run

		registry.case(name, make_starter_case(), tags=["http", "smoke", "uce", "public", "starter"])
