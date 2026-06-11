from pathlib import Path

from run_network_tests import TestFailure

ERROR_MARKERS = [
	"compile error",
	"runtime error",
	"timed out acquiring compile lock",
	"near line",
]


def _repo_root():
	return Path(__file__).resolve().parents[2]


def _parse_manifest(manifest_path):
	manifest = {}
	if not manifest_path.exists():
		return manifest
	for raw_line in manifest_path.read_text(encoding="utf-8").splitlines():
		line = raw_line.strip()
		if not line or line.startswith("#"):
			continue
		parts = [part.strip() for part in line.split("|")]
		if len(parts) < 7:
			continue
		file_name, title, description, tags, expected, suite, index = parts[:7]
		manifest[file_name] = {
			"file": file_name,
			"title": title,
			"description": description,
			"tags": tags.split(),
			"expected": expected,
			"suite": suite == "1",
			"index": index == "1",
		}
	return manifest


def _case_name(file_name):
	if file_name == "index.uce":
		return "site tests index"
	if file_name == "io.uce":
		return "site tests filesystem"
	if file_name == "websockets.ws.uce":
		return "site tests websockets page"
	stem = file_name
	if stem.endswith(".uce"):
		stem = stem[:-4]
	if stem.endswith(".ws"):
		stem = stem[:-3]
	return "site tests " + stem.replace("_", " ")


def _make_missing_metadata_case(file_name):
	def run(context):
		raise TestFailure("site/tests/%s is missing from site/tests/manifest.txt" % file_name)
	return run


def _make_suite_case(page_path, expected_title):
	def run(context):
		response = context.expect_status(page_path, 200)
		context.expect_body_contains(response, expected_title)

		body_lower = response.text.lower()
		for marker in ERROR_MARKERS:
			if marker in body_lower:
				raise Exception("response body contained error marker %r for %s" % (marker, page_path))
		if '<div class="tests-summary">' in body_lower and ('>fail</span>' in body_lower or '>failed 0<' not in body_lower):
			raise Exception("site suite reported failed cases for %s" % page_path)

		return "HTTP 200 with suite page marker and no failed cases for %s" % page_path

	return run


def register(registry):
	repo = _repo_root()
	tests_dir = repo / "site" / "tests"
	manifest = _parse_manifest(tests_dir / "manifest.txt")

	for path in sorted(tests_dir.glob("*.uce")):
		file_name = path.name
		meta = manifest.get(file_name)
		if not meta:
			registry.case(
				_case_name(file_name),
				_make_missing_metadata_case(file_name),
				tags=["http", "suite", "uce", "metadata", "internal"],
			)
			continue
		if not meta["suite"]:
			continue
		tags = list(meta["tags"])
		for tag in ["http", "suite", "uce"]:
			if tag not in tags:
				tags.append(tag)
		registry.case(
			_case_name(file_name),
			_make_suite_case("/tests/" + file_name, meta["expected"] or meta["title"]),
			tags=tags,
		)
