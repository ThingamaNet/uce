#!/bin/python3

import argparse
import http.client
import importlib.util
import json
import re
import socket
import sys
import time
from urllib.parse import urlparse

from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Optional


@dataclass
class Target:
	scheme: str = "http"
	host: str = "localhost"
	port: int = 80
	host_header: Optional[str] = None
	timeout: float = 5.0

	@property
	def label(self) -> str:
		return "%s://%s:%s" % (self.scheme, self.host, self.port)


@dataclass
class HttpResponse:
	status: int
	reason: str
	headers: Dict[str, str]
	body: bytes
	duration_ms: float

	@property
	def text(self) -> str:
		return self.body.decode("utf-8", errors="replace")


@dataclass
class TestResult:
	name: str
	ok: bool
	summary: str
	plugin: str
	duration_ms: float = 0.0
	details: str = ""
	tags: List[str] = field(default_factory=list)


class TestFailure(Exception):
	pass


class TestContext:
	def __init__(self, target: Target, args: argparse.Namespace):
		self.target = target
		self.args = args

	def request(
		self,
		path: str,
		method: str = "GET",
		headers: Optional[Dict[str, str]] = None,
		body: Optional[bytes] = None,
		timeout: Optional[float] = None,
	) -> HttpResponse:
		request_headers = dict(headers or {})
		if self.target.host_header and "Host" not in request_headers:
			request_headers["Host"] = self.target.host_header

		connection_class = http.client.HTTPConnection
		if self.target.scheme == "https":
			connection_class = http.client.HTTPSConnection

		started_at = time.perf_counter()
		connection = connection_class(self.target.host, self.target.port, timeout=timeout or self.target.timeout)
		try:
			connection.request(method, path, body=body, headers=request_headers)
			response = connection.getresponse()
			content = response.read()
			return HttpResponse(
				status=response.status,
				reason=response.reason,
				headers=dict(response.getheaders()),
				body=content,
				duration_ms=(time.perf_counter() - started_at) * 1000.0,
			)
		finally:
			connection.close()

	def expect_status(self, path: str, expected_status: int = 200, method: str = "GET") -> HttpResponse:
		response = self.request(path, method=method)
		if response.status != expected_status:
			raise TestFailure(
				"expected HTTP %s for %s, got %s %s" % (
					expected_status,
					path,
					response.status,
					response.reason,
				)
			)
		return response

	def expect_body_contains(self, response: HttpResponse, needle: str) -> None:
		if needle not in response.text:
			raise TestFailure("response body did not contain expected text: %r" % needle)

	def tcp_connect(self, host: Optional[str] = None, port: Optional[int] = None, timeout: Optional[float] = None) -> None:
		sock = socket.create_connection(
			(host or self.target.host, port or self.target.port),
			timeout or self.target.timeout,
		)
		sock.close()


@dataclass
class NetworkTestCase:
	name: str
	callback: Callable[[TestContext], Optional[str]]
	plugin: str
	tags: List[str] = field(default_factory=list)

	def run(self, context: TestContext) -> TestResult:
		started_at = time.perf_counter()
		try:
			summary = self.callback(context) or "ok"
			return TestResult(
				name=self.name,
				ok=True,
				summary=summary,
				plugin=self.plugin,
				duration_ms=(time.perf_counter() - started_at) * 1000.0,
				tags=list(self.tags),
			)
		except TestFailure as exc:
			return TestResult(
				name=self.name,
				ok=False,
				summary=str(exc),
				plugin=self.plugin,
				duration_ms=(time.perf_counter() - started_at) * 1000.0,
				tags=list(self.tags),
			)
		except Exception as exc:
			return TestResult(
				name=self.name,
				ok=False,
				summary="unexpected error: %s" % exc,
				plugin=self.plugin,
				duration_ms=(time.perf_counter() - started_at) * 1000.0,
				details=repr(exc),
				tags=list(self.tags),
			)


class TestRegistry:
	def __init__(self):
		self._cases = []
		self._active_plugin = ""

	def set_plugin(self, plugin_name: str) -> None:
		self._active_plugin = plugin_name

	def case(self, name: str, callback: Callable[[TestContext], Optional[str]], tags: Optional[Iterable[str]] = None) -> None:
		if not self._active_plugin:
			raise RuntimeError("plugin name was not set before registering tests")
		self._cases.append(
			NetworkTestCase(
				name=name,
				callback=callback,
				plugin=self._active_plugin,
				tags=list(tags or []),
			)
		)

	@property
	def cases(self) -> List[NetworkTestCase]:
		return list(self._cases)


def discover_plugin_files(plugin_dir: Path) -> List[Path]:
	if not plugin_dir.exists():
		return []
	return sorted(
		path for path in plugin_dir.iterdir()
		if path.is_file() and path.suffix == ".py" and path.name != "__init__.py"
	)


def load_plugin_module(module_path: Path):
	module_name = "network_test_plugin_%s" % module_path.stem
	spec = importlib.util.spec_from_file_location(module_name, str(module_path))
	if spec is None or spec.loader is None:
		raise RuntimeError("could not load plugin %s" % module_path)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


def register_plugin_cases(registry: TestRegistry, module_path: Path) -> None:
	module = load_plugin_module(module_path)
	if not hasattr(module, "register"):
		raise RuntimeError("plugin %s does not define register(registry)" % module_path.name)
	registry.set_plugin(module_path.stem)
	module.register(registry)


def filter_cases(
	cases: List[NetworkTestCase],
	plugins: List[str],
	name_pattern: Optional[str],
	tags: List[str],
	include_internal: bool,
) -> List[NetworkTestCase]:
	filtered = list(cases)
	if plugins:
		filtered = [case for case in filtered if case.plugin in plugins]
	if not include_internal:
		filtered = [case for case in filtered if "internal" not in case.tags]
	if name_pattern:
		matcher = re.compile(name_pattern)
		filtered = [case for case in filtered if matcher.search(case.name)]
	if tags:
		filtered = [case for case in filtered if any(tag in case.tags for tag in tags)]
	return filtered


def print_case_list(cases: List[NetworkTestCase]) -> None:
	for case in cases:
		tag_suffix = ""
		if case.tags:
			tag_suffix = " tags=%s" % ",".join(case.tags)
		print("%s:%s%s" % (case.plugin, case.name, tag_suffix))


def print_result(result: TestResult) -> None:
	status = "PASS" if result.ok else "FAIL"
	print("[%s] %s:%s (%.1f ms) - %s" % (
		status,
		result.plugin,
		result.name,
		result.duration_ms,
		result.summary,
	))
	if result.details:
		print("       %s" % result.details)


def write_json_report(results: List[TestResult], report_path: Path) -> None:
	report_path.write_text(json.dumps([asdict(result) for result in results], indent=2) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
	base_dir = Path(__file__).resolve().parent
	parser = argparse.ArgumentParser(description="Dependency-less plugin-style network test runner")
	parser.add_argument("--base-url", help="full base URL, for example http://k-uce or http://127.0.0.1:8080")
	parser.add_argument("--host", default="localhost", help="target host or IP")
	parser.add_argument("--port", type=int, default=80, help="target port")
	parser.add_argument("--scheme", default="http", choices=["http", "https"], help="target scheme")
	parser.add_argument("--host-header", default="uce.openfu.com", help="optional Host header override")
	parser.add_argument("--timeout", type=float, default=5.0, help="per-request timeout in seconds")
	parser.add_argument("--plugin-dir", default=str(base_dir / "plugins"), help="directory containing plugin files")
	parser.add_argument("--plugin", action="append", default=[], help="limit to one or more plugin basenames")
	parser.add_argument("--match", help="regex filter for case names")
	parser.add_argument("--tag", action="append", default=[], help="only run cases with at least one matching tag")
	parser.add_argument("--include-internal", action="store_true", help="include tests tagged internal in the run")
	parser.add_argument("--list", action="store_true", help="list discovered tests without running them")
	parser.add_argument("--fail-fast", action="store_true", help="stop on first failing test")
	parser.add_argument("--json-report", help="write full results to a JSON file")
	return parser


def build_target(args: argparse.Namespace) -> Target:
	if args.base_url:
		parsed = urlparse(args.base_url)
		if parsed.scheme not in ("http", "https") or not parsed.hostname:
			raise SystemExit("--base-url must include an http or https scheme and hostname")
		default_port = 443 if parsed.scheme == "https" else 80
		return Target(
			scheme=parsed.scheme,
			host=parsed.hostname,
			port=parsed.port or default_port,
			host_header=args.host_header or parsed.hostname,
			timeout=args.timeout,
		)

	return Target(
		scheme=args.scheme,
		host=args.host,
		port=args.port,
		host_header=args.host_header or None,
		timeout=args.timeout,
	)


def main(argv: Optional[List[str]] = None) -> int:
	args = build_parser().parse_args(argv)
	plugin_dir = Path(args.plugin_dir).resolve()
	plugin_files = discover_plugin_files(plugin_dir)
	if not plugin_files:
		print("No plugin files found in %s" % plugin_dir, file=sys.stderr)
		return 2

	registry = TestRegistry()
	for plugin_path in plugin_files:
		register_plugin_cases(registry, plugin_path)

	cases = filter_cases(registry.cases, args.plugin, args.match, args.tag, args.include_internal)
	if not cases:
		print("No tests matched the selected filters", file=sys.stderr)
		return 2

	if args.list:
		print_case_list(cases)
		return 0

	context = TestContext(
		target=build_target(args),
		args=args,
	)

	print("Running %s test(s) against %s" % (len(cases), context.target.label))
	if context.target.host_header:
		print("Host header: %s" % context.target.host_header)

	results = []
	for case in cases:
		result = case.run(context)
		results.append(result)
		print_result(result)
		if args.fail_fast and not result.ok:
			break

	if args.json_report:
		write_json_report(results, Path(args.json_report).resolve())

	failures = [result for result in results if not result.ok]
	print("")
	print("Summary: %s passed, %s failed" % (len(results) - len(failures), len(failures)))
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())