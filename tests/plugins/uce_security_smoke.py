import http.client

from run_network_tests import TestFailure


def _direct_http_request(path, headers=None):
	connection = http.client.HTTPConnection("127.0.0.1", 8080, timeout=5.0)
	try:
		connection.request("GET", path, headers=headers or {})
		response = connection.getresponse()
		body = response.read().decode("utf-8", errors="replace")
		return response.status, dict(response.getheaders()), body
	finally:
		connection.close()


def register(registry):
	def direct_http_rejects_dotdot(context):
		status, headers, body = _direct_http_request("/../site/demo/hello.uce")
		if status == 200 or "hello world" in body:
			raise TestFailure("direct HTTP accepted dot-dot script traversal")
		return "direct HTTP dot-dot traversal rejected with HTTP %s" % status

	def direct_http_ignores_script_filename_header(context):
		status, headers, body = _direct_http_request(
			"/no-such-script.uce",
			headers={"Script-Filename": "/Code/uce.openfu.com/uce/site/demo/hello.uce"},
		)
		if status == 200 or "hello world" in body:
			raise TestFailure("direct HTTP trusted client Script-Filename header")
		return "direct HTTP Script-Filename override rejected with HTTP %s" % status

	def response_headers_are_sanitized(context):
		response = context.request("/tests/security_headers.uce")
		if "X-UCE-Injected" in response.headers or "X-UCE-Redirect-Injected" in response.headers:
			raise TestFailure("CRLF header injection produced an extra response header")
		location = response.headers.get("Location", "")
		if "\r" in location or "\n" in location:
			raise TestFailure("Location header contains raw CR/LF")
		return "CRLF response header injection was sanitized"

	registry.case("direct HTTP rejects dot-dot script traversal", direct_http_rejects_dotdot, tags=["security", "http", "internal"])
	registry.case("direct HTTP ignores Script-Filename header", direct_http_ignores_script_filename_header, tags=["security", "http", "internal"])
	registry.case("response headers sanitize CRLF", response_headers_are_sanitized, tags=["security", "http", "internal"])
