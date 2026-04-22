def register(registry):
	def port_80(context):
		context.tcp_connect(port=80)
		return "TCP connect succeeded on port 80"

	registry.case("frontend port 80", port_80, tags=["tcp", "smoke", "uce", "public"])

	def port_8080(context):
		context.tcp_connect(port=8080)
		return "TCP connect succeeded on port 8080"

	registry.case("http websocket port 8080", port_8080, tags=["tcp", "uce", "internal"])