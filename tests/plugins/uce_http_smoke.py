def register(registry):
	pages = [
		("doc index", "/doc/index.uce", "<html>"),
		("doc singlepage", "/doc/singlepage.uce", "<html>"),
		("doc component page", "/doc/index.uce?p=component", "component()"),
		("doc relative time page", "/doc/index.uce?p=time_format_relative", "time_format_relative"),
	]

	for name, path, needle in pages:
		def make_case(page_path=path, expected_text=needle):
			def run(context):
				response = context.expect_status(page_path, 200)
				context.expect_body_contains(response, expected_text)
				return "HTTP 200 with expected body marker for %s" % page_path

			return run

		registry.case(name, make_case(), tags=["http", "smoke", "uce", "public"])