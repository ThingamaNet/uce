ERROR_MARKERS = [
	"compile error",
	"runtime error",
	"timed out acquiring compile lock",
	"near line",
]


def register(registry):
	pages = [
		("site tests index", "/tests/index.uce", "Coverage Index", ["http", "suite", "uce", "public"]),
		("site tests core", "/tests/core.uce", "Core APIs", ["http", "suite", "uce", "public"]),
		("site tests http", "/tests/http.uce", "HTTP And Session", ["http", "suite", "uce", "public"]),
		("site tests components", "/tests/components.uce", "Components", ["http", "suite", "uce", "public"]),
		("site tests markdown", "/tests/markdown.uce", "Markdown", ["http", "suite", "uce", "public"]),
		("site tests units", "/tests/units.uce", "Units", ["http", "suite", "uce", "public"]),
		("site tests websockets page", "/tests/websockets.ws.uce", "WebSockets", ["http", "suite", "uce", "public", "websocket"]),
		("site tests filesystem", "/tests/io.uce", "Filesystem", ["http", "suite", "uce", "internal"]),
		("site tests services", "/tests/services.uce", "Sockets And Services", ["http", "suite", "uce", "internal"]),
		("site tests tasks", "/tests/tasks.uce", "Tasks", ["http", "suite", "uce", "internal"]),
	]

	for name, path, title, tags in pages:
		def make_case(page_path=path, expected_title=title):
			def run(context):
				response = context.expect_status(page_path, 200)
				context.expect_body_contains(response, expected_title)

				body_lower = response.text.lower()
				for marker in ERROR_MARKERS:
					if marker in body_lower:
						raise Exception("response body contained error marker %r for %s" % (marker, page_path))

				return "HTTP 200 with suite page marker for %s" % page_path

			return run

		registry.case(name, make_case(), tags=tags)