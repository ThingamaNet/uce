# Network Tests

This directory contains a dependency-less vanilla Python 3 network test runner and plugin-style test cases. These are AI-generated so they might be garbage.

## Runner

Run the default local UCE battery:

```bash
./tests/run_network_tests.py
/bin/python3 tests/run_network_tests.py
```

The default battery is aimed at public or cross-host LAN checks and currently focuses on stable published doc endpoints, the published `site/tests` public coverage pages, plus a basic TCP reachability check.
Internal-only cases are tagged `internal` and are not part of the default smoke path unless you ask for them explicitly.

For the local runtime, the target is localhost rather than `http://k-uce`.

Run against another machine on the local network:

```bash
/bin/python3 tests/run_network_tests.py --base-url http://k-uce --host-header uce.openfu.com
```

Explicit localhost example:

```bash
/bin/python3 tests/run_network_tests.py --base-url http://localhost --host-header uce.openfu.com
```

List discovered cases without running them:

```bash
/bin/python3 tests/run_network_tests.py --list
```

Run only one plugin or a subset of tagged tests:

```bash
/bin/python3 tests/run_network_tests.py --plugin uce_http_smoke
/bin/python3 tests/run_network_tests.py --tag tcp
/bin/python3 tests/run_network_tests.py --match session
/bin/python3 tests/run_network_tests.py --tag internal --include-internal
```

Override the network target:

```bash
/bin/python3 tests/run_network_tests.py --host 10.8.0.12 --port 80 --host-header uce.openfu.com
/bin/python3 tests/run_network_tests.py --base-url http://10.8.0.12:8080
```

Write a JSON report:

```bash
/bin/python3 tests/run_network_tests.py --json-report tests/last-report.json
```

## Plugin Contract

Each plugin is a plain Python file under `tests/plugins/` that exports:

```python
def register(registry):
	...
```

Use `registry.case(name, callback, tags=[...])` to register cases.

Each callback receives a `TestContext` and may use helpers such as:

- `context.request(...)`
- `context.expect_status(...)`
- `context.expect_body_contains(...)`
- `context.tcp_connect(...)`

## Included Plugins

- `uce_http_smoke.py`: HTTP 200/body-marker checks for stable published doc pages
- `uce_site_suite.py`: HTTP 200/body-marker checks for the published `site/tests` coverage pages; local-only pages are tagged `internal`
- `uce_tcp_smoke.py`: basic TCP listener checks, with public smoke checks on port 80 and an internal-only HTTP/WebSocket listener probe on port `8080` tagged `internal`