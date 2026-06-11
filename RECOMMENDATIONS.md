# UCE Code Review — Full Findings (2026-06-11)

Scope: the pending working-tree changes (73 modified tracked files plus new
untracked sources — notably `src/lib/sqlite-connector.cpp/.h` and the
uce-starter theme/router rework).

Method: seven independent review angles (line-by-line diff scan,
removed-behavior audit, cross-file call tracing, reuse, simplification,
efficiency, altitude), followed by a verification pass on the correctness
candidates. Status legend:

- **Confirmed** — verified against the working tree, decisive lines quoted.
- **Plausible** — surfaced by a review angle, not individually re-verified.
- **Refuted** — investigated and found not to be an issue (kept here so
  nobody re-flags it).

---

## Part 1 — Correctness (all confirmed)

### 1.1 XSS: island props break out of single-quoted attribute

**File:** `site/examples/uce-starter/components/theme/web_affordances.uce:12`

The island component emits JSON props inside a single-quoted attribute
(`data-props='<?: html_escape(payload) ?>'`), but `html_escape()`
(`src/lib/functionlib.cpp:936`) escapes only `& < > "`, and `json_encode` /
`json_escape` (`src/lib/dtree.cpp:794`) never escape apostrophes. Any `'` in a
prop value terminates the attribute.

**Failure:** a view passes user-influenced prop text containing an apostrophe,
e.g. `x' autofocus onfocus='alert(1)` — the attribute terminates early and
attacker-controlled attributes/event handlers land on the div. Even benign
values like `Don't` silently truncate the payload.

**Fix:** use a double-quoted attribute (html_escape covers `"`), or extend
`html_escape` to escape `'` as `&#39;`.

**Status:** fixed, but fix not verified yet.

### 1.2 Crash: positional `?` placeholders in sqlite_query

**File:** `src/lib/sqlite-connector.cpp:86`

`bind_params` constructs a `String` directly from
`sqlite3_bind_parameter_name(stmt, i)`, which returns NULL for nameless
positional `?` parameters. `std::string(nullptr)` is UB and crashes before the
`if(name == "") continue;` guard on the next line can run.

**Failure:** any page calling
`sqlite_query(db, "select * from notes where id = ?", params)` with a bare `?`
instead of `:name` segfaults the worker (500 recovery page).

**Fix:** remove support for positional placeholders from sqlite and mysql API in favor of named placeholders, adjust the documentation accordingly.

**Status:** fixed, but fix not verified yet.

### 1.3 HTTP parsing: split_http_headers assumes lines[0] is the request line

**File:** `src/lib/functionlib.cpp:754`

The rewritten parser unconditionally treats the first line as the HTTP request
line. The old parser located the first colon-free line, which tolerated a
leading CRLF (RFC 7230 §3.5) and header-only input. Two failure modes:

- The direct-HTTP caller (`src/fastcgi/src/fcgicc.cc:693`) passes the raw
  buffer unstripped — a client sending a stray leading CRLF before
  `GET /x.uce HTTP/1.1` gets the request line silently dropped (empty
  `REQUEST_METHOD`, request fails).
- Header-only input (`Host: example.test\nX-Token: abc`, e.g. page code
  parsing a raw header block) parses `Host:` as `REQUEST_METHOD` and loses
  `HTTP_HOST` entirely. The function is directly callable from .uce pages.

**Fix:** skip leading empty lines before consuming the request line, and only
treat a colon-free first line as the request line.

**Status:** fixed, but fix not verified yet.

### 1.4 Silent data loss: multi-statement SQL drops everything after the first `;`

**File:** `src/lib/sqlite-connector.cpp:178`

`SQLite::query` passes `0` for `pzTail` to `sqlite3_prepare_v2` and never
checks for remaining SQL, so multi-statement strings execute only the first
statement and still report `ok`.

**Failure:** a migration page runs
`sqlite_query(db, "create table t(id integer); insert into t values(1);")` —
only the CREATE executes, the INSERT is silently dropped, `sqlite_error()`
says `ok`, leaving the database half-migrated with no error signal.

**Fix:** capture `pzTail` and either loop over remaining statements or raise
an error when trailing SQL is present.

**Status:** fixed, but fix not verified yet.

### 1.5 Diagnostics regression: fault backtrace captured after siglongjmp

**File:** `src/linux_fastcgi.cpp:895`

The in-handler `capture_backtrace_string` call (`request_fault_trace`) was
deleted; the trace is now captured after `siglongjmp` back in
`handle_complete`, where the faulting stack has already been unwound. No
capture remains in `on_request_fault_signal`.

**Failure:** a `.uce` page null-derefs → SIGSEGV → the error page's Trace
shows only `handle_complete`/`main` frames. The faulting unit's frames are
gone, making crash reports undiagnosable beyond the signal number (the README
still advertises a native backtrace of the failure).

**Fix:** restore the capture inside `on_request_fault_signal` (on the faulting
stack) and hand the result across the longjmp, as the previous code did.

**Status:** fixed, but fix not verified yet.

### 1.7 Memory leak: SQLite wrapper objects never freed by request cleanup

**File:** `src/lib/sqlite-connector.cpp:248`

`cleanup_sqlite_connections()` closes the raw `sqlite3` handles tracked in
`resources.sqlite_connections` but never deletes the heap-allocated `SQLite`
wrappers from `sqlite_connect` (`new` without `delete`; no arena allocator is
compiled in). `sqlite_disconnect` itself is correct (`delete db` after
unregistering). Note: this mirrors a pre-existing identical leak in the MySQL
connector (`cleanup_mysql_connections`, `mysql-connector.cpp:298-304`).

**Failure:** a page calls `sqlite_connect()` per request and relies on
end-of-request cleanup instead of `sqlite_disconnect()` — exactly what the
cleanup path exists for. One wrapper leaks per request; unbounded RSS growth
in the long-lived FastCGI worker.

**Fix:** track the wrapper objects (not raw handles) in
`resources.sqlite_connections` and delete them in cleanup; fix the MySQL
connector the same way, or factor a shared registry (see 5.1).

**Status:** fixed, but fix not verified yet.

### 1.8 Asset shims emit stylesheets mid-`<body>`

**File:** `site/examples/uce-starter/components/example/marketing_assets.uce:8`
(also `theme_assets.uce`, `gauges/assets.uce`)

The new ONCE-based asset shims fire inside the view's `ob_start()` capture in
`index.uce` (lines 67-85: view is captured into `fragments["main"]`, then
`themes/page` renders), so their `<link rel="stylesheet">` output is baked
into the main fragment and spliced into the content div — inside `<body>`,
not `<head>`.

**Failure:** every marketing/theme/gauges page ships its stylesheet mid-body
(FOUC, invalid-ish markup). The deleted `page_shell` asset registry rendered
these in `<head>`. While this behavior is often okay in practice, we should
improve on this to encourage cleaner output.

**Fix:** Part A: restore a registration mechanism: record component and asset output
that's intended as once per page in context.call["fragments"]["once"] by default
and the page template component can then explicitly slot this in where appropriate.

Part B: Modify the preprocessor so directives support attributes like this:
ONCE(Request& context)
@fragment my-fragment-name
{
  ...
}

Which will then automatically (in this example) slot the output into
context.call["fragments"]["my-fragment-name"] instead of the default slot name "once". 
In the future we'll introduce more attributes with this syntax. Using this flexible mechanism for the fragment slot
name, we can leave it up to the page template where to slot in what. ONCE, COMPONENT,
and RENDER should support the fragment attribute. 

**Status:** fixed, but fix not verified yet.

### 1.9 Error page "Generated C++" hint prints a nonexistent path

**File:** `src/linux_fastcgi.cpp:79`

`render_request_failure` computes the hint as
`path_join(BIN_DIRECTORY, SCRIPT_FILENAME) + ".cpp"`, but `path_join` returns
an absolute child unchanged (`src/lib/sys.cpp:179`), while the compiler builds
the real path by plain concatenation `BIN_DIRECTORY + src_path`
(`src/lib/compiler.cpp:631-636`). SCRIPT_FILENAME is always absolute in the
FastCGI path.

**Failure:** every runtime failure page prints e.g.
`Generated C++: /var/www/site/page.uce.cpp` — the BIN_DIRECTORY prefix is
silently dropped and the path never exists (real file:
`/var/cache/uce/work/var/www/site/page.uce.cpp`).

**Fix:** export one helper from `compiler.cpp` that maps a source file to its
generated artifact (`su->pre_path + "/" + su->pre_file_name` — the new
`compiler_format_compile_failure` in this same changeset already computes it
correctly) and use it in both places.

**Status:** fixed, but fix not verified yet.

### 1.10 O(n²) and per-row deep copies in dtree_map / dtree_filter

**File:** `src/lib/functionlib.cpp:182`

Both helpers call `tree.is_list()` inside the per-element callback —
`is_list()` (`src/lib/dtree.cpp:165`) walks the entire map, making the
operation O(n²) — even though both already compute `is_list()` once before
the loop. Additionally, `DTree::each` (`dtree.h:22`) takes the callback
element by value (`DTree t`), deep-copying every subtree per iteration.

**Failure:** mapping over a 1000-row `sqlite_query` result does ~1M
key-validation checks plus a full deep copy of each row tree.

**Fix:** hoist `is_list()` into a `bool` local reused in the callback; change
`each()` to pass `const DTree&`.

**Status:** fixed, but fix not verified yet.

### 1.11 Proactive compiler child can std::terminate on lock failure

**File:** `src/lib/compiler.cpp:288`

`compiler_with_registry_lock` now throws `std::runtime_error` when the
registry lock file can't be opened (previously: warning + proceed unlocked).
`run_proactive_compiler` (`src/linux_fastcgi.cpp:1270-1372`) calls
`compiler_list_known_units` / `compiler_set_known_units` with no try/catch
anywhere in the chain, so a transient failure (fd exhaustion, disk full)
aborts the forked child via `std::terminate`.

**Blast radius is small:** the proactive compiler runs in a forked child that
the main loop respawns each iteration, and request-path callers are protected
by the try/catch in `handle_complete`. Still, a catch-and-log around the
proactive scan is cheap insurance.

**Status:** fixed, but fix not verified yet.

---

## Part 2 — Reuse / duplication (plausible unless noted)

### 2.1 request_query_path() duplicates request_query_route()

**File:** `src/lib/uri.cpp:311`

`request_query_path()` copy-pastes `request_query_route()`'s (line 325)
first-keyless-segment scan verbatim — two identical "find first &-part
without =" loops plus `route_path_sanitize` calls that must be kept in sync.
It also has **zero callers** in `src/` or `site/` (only a doc page references
it).

**Fix:** implement as
`return request_query_route(context, default_path)["l_path"].to_string();`.

**Status:** fixed, but fix not verified yet.

### 2.2 list_filter / list_map re-implement the generic filter<T> / map templates

**File:** `src/lib/functionlib.cpp:71`

`StringList` is `typedef std::vector<String>` (`types.h:52`), so the existing
`filter<T>` template (`functionlib.h:68`, declared ~10 lines above the new
`list_*` declarations) already does exactly what `list_filter` does. Two
filter implementations now live in the same module; behavior fixes must be
applied twice.

**Fix:** drop it and point the doc page at `filter`); same consideration for `list_map`.

**Status:** fixed, but fix not verified yet.

### 2.3 Hand-rolled redirects instead of the redirect() helper

**Files:** `site/examples/uce-starter/views/account/login.uce:13`,
`logout.uce:6`, `profile.uce:8`, `site/demo/sqlite.uce:21-22`

Four call sites inline `context.set_status(302, "Found");
context.header["Location"] = ...` instead of calling the existing runtime
helper `redirect(String url, s32 code = 302)` (`src/lib/uri.cpp:414`). This
replaced the single `app_redirect()` helper the changeset deleted.

**Note — security aspect refuted (see 6.1):** headers are sanitized centrally
on write-out, so this is a pure reuse nit, not a vulnerability.

**Fix:** use `redirect(app_link("account/profile", context))` or restore one
`app_redirect` wrapper.

**Status:** fixed, but fix not verified yet.

### 2.5 Asset tag boilerplate repeated across ~a dozen files

**Files:** `components/example/marketing_assets.uce:5`,
`components/example/theme_assets.uce`, `components/gauges/assets.uce`,
plus inline `<link rel="stylesheet" href="<?= app_asset_url(...) ?>" />`
blocks in `components/theme/head.uce`, `components/data/widgets.uce`,
`components/workspace/primitives.uce`, `views/dashboard.uce`

About a dozen hand-written copies of the stylesheet/script tag pattern around
`app_asset_url()`; a versioning or attribute change (defer/integrity) touches
every file. The marketing and theme shims differ only in one CSS path.

**Fix:** collapse the three shims into one parameterizable assets component.

**Status:** fixed, but fix not verified yet.

---

## Part 3 — Simplification (plausible)

### 3.1 request_query_route() emits a derivable "rejected" flag

**File:** `src/lib/uri.cpp:347`

The route tree contains both `route["valid"]` and `route["rejected"]` where
`rejected` is exactly `!valid`, and nothing in the codebase reads
`"rejected"`. Redundant derivable state doubles the invariant surface — a
future path that sets one flag without the other produces a route tree that
lies.

**Fix:** drop the `"rejected"` key; callers needing it can write
`!route["valid"].to_bool()`.

**Status:** fixed, but fix not verified yet.

### 3.2 Starter router carries write-only diagnostic state

**File:** `site/examples/uce-starter/index.uce:38`

The router rewrite adds candidate `kind`/`matched` fields and
`context.call["route"]["candidates"]/["resolved"]/["missed"]`, none of which
is read by any component, theme, or test. It also uses `dtree_filter` to
`file_exists`-stat every candidate after the first match is already found.
~55 lines plus a builder helper replace what the deleted `app_resolve_view`
did with three early-return `file_exists` checks.

**Fix:** remove unused parts.

**Status:** fixed, but fix not verified yet.

### 3.4 Single-link shim components where a direct ONCE block suffices

**File:** `site/examples/uce-starter/components/example/marketing_assets.uce:8`
(and `theme_assets.uce`)

These are single-`<link>` shim files with empty COMPONENT bodies, invoked via
`print(component(...))` of an empty string. Routed views are mutually
exclusive per request, so the cross-unit dedup the shims provide can never
trigger; they exist only to host one ONCE line at the cost of two extra files
and an indirection on every view. `dashboard.uce:3-6` already demonstrates
the simpler form (ONCE block directly in the view). `gauges/assets.uce` is
the justified case (multiple sibling components per page) and can stay.

**Status:** fixed, but fix not verified yet.

### 3.5 Compile-failure artifact file is self-referential

**File:** `src/lib/compiler.cpp:847`

`compile_shared_unit()` overwrites `su->compiler_messages` with the formatted
failure report, then writes that report into `compile_output_file_name` — the
same artifact the report's own "Compile output:" line
(`compiler_format_compile_failure`, line 800) points readers at. No copy of
the raw, unformatted compiler output survives for tooling to parse.

**Fix:** keep raw messages as the stored/recorded artifact content and format
only at the print/response boundary.

**Status:** fixed, but fix not verified yet.

---

## Part 4 — Efficiency (plausible unless noted)

### 4.1 QUERY_STRING parsed twice per request

**File:** `src/linux_fastcgi.cpp:744`

`prepare_request_body_maps` calls `request_populate_context_params` (which
splits QUERY_STRING on `&` + uri-decodes inside `request_query_route`) and
then `parse_query(QUERY_STRING)` re-splits and re-decodes the identical
string on the next line. Runs on every HTTP request, CLI invocation, and
websocket event. Route params are also computed eagerly for requests that
never read `ROUTE_*`/`BASE_URL`.

**Fix:** parse QUERY_STRING once into `request.get` first and derive the
route token from that single pass, or populate the route params lazily.

**Status:** fixed, but fix not verified yet.

### 4.2 Repeated normalization in request_populate_context_params

**File:** `src/lib/uri.cpp:351`

Per request: `route_path_normalize` runs three times on the same path
(directly, inside `route_path_sanitize`, and inside `route_path_is_safe`),
`request_script_url` is computed twice (for SCRIPT_URL and again inside
`request_base_url`), and `route_path_normalize` (line 255) strips slashes via
`path = path.substr(1)` in a while loop — O(n²) copies per slash run.

**Fix:** normalize once and pass the normalized string down; compute
script_url into a local used by both params; replace the substr loops with
`find_first_not_of("/")` / `find_last_not_of("/")` and a single substr.

**Status:** fixed, but fix not verified yet.

### 4.4 collect_rows rebuilds column-name strings for every row

**File:** `src/lib/sqlite-connector.cpp:118`

For R rows × C columns the row loop does R×C `sqlite3_column_name` calls plus
R×C String heap constructions and map inserts keyed on the fresh string,
though column names are invariant across rows. A 10k-row, 8-column result
builds 80k redundant name strings per query.

**Fix:** build a `std::vector<String> names(column_count)` once before the
step loop and index it inside (`row[names[i]]`).

**Status:** fixed, but fix not verified yet.

### 4.5 bind_params copies the params map twice and binds SQLITE_TRANSIENT

**File:** `src/lib/sqlite-connector.cpp:95`

`SQLite::query` takes `StringMap` by value and passes it by value again to
`bind_params`, then binds with `SQLITE_TRANSIENT`, forcing SQLite to memcpy
each value a third time — even though the copied map outlives the statement
(it lives until after `sqlite3_finalize`).

**Fix:** pass `const StringMap&` through `query()`/`bind_params` and bind
with `SQLITE_STATIC` (or at least drop the two by-value map copies).

**Status:** fixed, but fix not verified yet.

### 4.6 Per-request connection opens re-run pragmas; busy_timeout set twice

**File:** `src/lib/sqlite-connector.cpp:15`

`connect()` calls `sqlite3_busy_timeout(5000)` and then
`apply_default_pragmas` runs `PRAGMA busy_timeout = 5000` again (pure
duplicate). `PRAGMA journal_mode = WAL` is persistent in the database file
but re-issued on every open. Because cleanup closes all handles at request
end, a page like `site/demo/sqlite.uce` pays `sqlite3_open_v2` + 4 pragmas +
`CREATE TABLE IF NOT EXISTS` on every request.

**Fix:** drop the redundant busy_timeout pragma; cache open
connections per worker keyed by path across requests (resetting state at
request end) so pragmas and schema checks run once per worker.

**Status:** fixed, but fix not verified yet.

---

## Part 5 — Altitude / design (plausible unless noted)

### 5.3 Test suites hand-registered in three parallel lists — already drifting

**Files:** `site/tests/index.uce:15`, `tests/plugins/uce_site_suite.py`

A test page must be registered in three places: the `site/tests/*.uce` file
itself, a `site_tests_card()` line in `index.uce`, and a tuple in
`uce_site_suite.py`. The new `sqlite.uce` was added to all three by hand. The
lists have already drifted: `site/tests/call_helpers.uce` exists on disk but
appears in neither `index.uce` nor any plugin list, and
`security_headers.uce` is covered only by the security-smoke plugin, not the
index cards — new suites can silently fall out of the dashboard and/or CI.

**Fix:** enumerate `site/tests/*.uce` with ls()/glob in both `index.uce` and
`uce_site_suite.py`, with title/tags metadata declared once (in the test page
or a single shared manifest).

**Status:** fixed, but fix not verified yet.

### 5.4 compiler_developer_hints() couples the runtime to clang's English message text

**File:** `src/lib/compiler.cpp:778`

The hint table pattern-matches hardcoded English clang diagnostic substrings
("no member named", "expected ';'", ...). A clang version bump, a switch to
gcc (COMPILE_SCRIPT is user-configurable server config), or localized
diagnostics silently degrades every hint to the generic fallback, and each
new error class means hand-extending an if-chain in runtime C++.

**Fix:** the excerpt mechanism added in the same change
(`compiler_format_compile_failure`'s source/generated excerpts + artifact
paths) already carries the diagnostic value; make the hints a data-driven
table or drop them in favor of the excerpts.

**Status:** fixed, but fix not verified yet.

### 5.5 Generated-artifact path computed by hand in two places

**File:** `src/linux_fastcgi.cpp:79` vs `src/lib/compiler.cpp:631-636`

The deeper-fix framing of 1.9: the moment the compiler's artifact layout or
naming changes (hashing, per-config subdirs), any hand-recomputed path goes
stale. One exported source-file → artifact-path helper in `compiler.cpp`,
used by both the compile-failure formatter and the runtime failure page.

**Status:** fixed, but fix not verified yet.
