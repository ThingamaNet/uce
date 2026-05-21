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


def _frontend_http_request(path, headers=None):
	request_headers = {"Host": "uce.openfu.com"}
	request_headers.update(headers or {})
	connection = http.client.HTTPConnection("127.0.0.1", 80, timeout=5.0)
	try:
		connection.request("GET", path, headers=request_headers)
		response = connection.getresponse()
		body = response.read().decode("utf-8", errors="replace")
		return response.status, response.getheaders(), body
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
		injected_headers = [
			"X-UCE-Injected",
			"X-UCE-Injected-Name",
			"X-UCE-Cookie-Injected",
			"X-UCE-Redirect-Injected",
			"X-UCE-Status-Injected",
		]
		for header in injected_headers:
			if header in response.headers:
				raise TestFailure("CRLF header injection produced extra response header %s" % header)
		location = response.headers.get("Location", "")
		if "\r" in location or "\n" in location:
			raise TestFailure("Location header contains raw CR/LF")
		return "CRLF response header injection was sanitized"

	def unknown_session_id_is_not_adopted(context):
		attacker_id = "a" * 64
		status, headers, body = _frontend_http_request("/tests/http.uce", headers={"Cookie": "uce-site-tests=" + attacker_id})
		set_cookies = [value for name, value in headers if name.lower() == "set-cookie"]
		session_cookies = [value for value in set_cookies if value.startswith("uce-site-tests=")]
		if any(("uce-site-tests=" + attacker_id) in value for value in session_cookies):
			raise TestFailure("session_start adopted caller supplied unknown session id")
		if not session_cookies or not any("HttpOnly" in value and "SameSite=Lax" in value for value in session_cookies):
			raise TestFailure("session_start did not issue a hardened replacement session cookie")
		return "unknown caller-supplied session id was replaced"

	registry.case("direct HTTP rejects dot-dot script traversal", direct_http_rejects_dotdot, tags=["security", "http", "internal"])
	registry.case("direct HTTP ignores Script-Filename header", direct_http_ignores_script_filename_header, tags=["security", "http", "internal"])
	registry.case("response headers sanitize CRLF", response_headers_are_sanitized, tags=["security", "http", "internal"])
	registry.case("unknown session ids are not adopted", unknown_session_id_is_not_adopted, tags=["security", "http", "internal"])
