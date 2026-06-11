# WASM-PROPOSAL: WebAssembly Unit Runtime for UCE

- **Status:** proposal / design draft (2026-06-11)
- **Scope:** replace the native unit pipeline (generated C++ → clang → `.so` →
  `dlopen`) with per-unit WebAssembly modules executed in a per-request,
  runtime-linked workspace, exposing the same API surface to page code.
- **Origin:** design discussion 2026-06-11; incorporates the post-mortem of the
  earlier per-invocation arena attempt (preserved in
  `src/lib/_scratchpad.cpp`).

---

## 1. Motivation

Two long-standing structural problems and one strategic opportunity share a
single root cause: **request code shares an address space and an allocator
with the runtime.**

1. **The arena attempt failed for a structural reason.** The
   `GLOBAL_ARENA_ALLOCATOR` design in `_scratchpad.cpp` swapped the global
   `operator new`/`delete` against a `current_memory_arena`. A single global
   allocator cannot distinguish request-lifetime allocations from
   process-lifetime ones: during a request, server-lifetime structures
   (compile registry, sessions, config trees, unit statics) also allocate.
   Under the arena those dangle on reset; with `delete` as a no-op,
   system-allocated objects released mid-request leak. The lifetime
   distinction lives in the type system and call graph, not at the allocator
   boundary — fixing it in-process means threading PMR allocators through
   `String`, `DTree`, and every container.

2. **Fault recovery is best-effort, not sound.** The current
   SIGSEGV → `sigsetjmp`/`siglongjmp` recovery performs no unwinding, skips
   destructors, can resume over corrupted worker state, and (see
   RECOMMENDATIONS.md 1.5) cannot reliably produce a useful backtrace.
   A faulting unit *can* have scribbled on runtime memory before the signal.

3. **UCE can only ever host fully-trusted code.** A `.uce` page is arbitrary
   native code with full process privileges. Multi-tenant or user-supplied
   page hosting is structurally impossible in the native model.

A WASM execution model deletes the shared-fate fact itself rather than
patching around it:

- **Arena by construction:** each request runs in its own linear memory,
  dropped wholesale at request end. Request-lifetime memory physically cannot
  outlive the request; server-lifetime state physically cannot live inside
  it. The `_scratchpad.cpp` design becomes correct because the boundary is
  structural, not typological.
- **Sound recovery:** a trap (null deref, OOB, stack exhaustion) is a defined
  host-side error that unwinds cleanly, with a precise guest stack trace, and
  the unit cannot have touched host memory. Render error page, drop
  workspace, keep serving — actually correct, not hopeful.
- **Capability security:** page code gets exactly the imported API surface
  and nothing else. Multi-tenant hosting becomes possible.
- **Secondary wins:** architecture-independent unit artifacts (no clang
  required on prod), safe module unload/replace (vs. never-safe `dlclose`),
  first-class limits (linear-memory cap = RAM limit, epoch/fuel = CPU
  timeout), per-request memory stats for free.

**Accepted costs:** ~1.2–2× compute slowdown vs. native (still far ahead of
interpreted runtimes); a real ABI/membrane design; ownership of a custom
loader; toolchain rough edges (§10).

---

## 2. Rejected alternatives (recorded so they stay rejected)

- **In-process PMR arena.** Requires re-typedefing `String`/`DTree` and
  threading allocators through the entire codebase; the failed global-swap
  shortcut is the only cheap version and it is unsound (§1.1).
- **One instance per component.** UCE components are function calls, not
  RPCs: callees receive the context **by reference**, mutate `context.call`,
  share the `ob_*` capture stack and `ONCE` dedup state. Per-component
  instances force serialize/copy/deserialize of the context on every
  `component()` call (a dozen+ per page in the starter), require
  host-mediation of the ob stack, and silently change reference semantics to
  copy semantics. Rejected.
- **One linked module per app ("the blob").** Introduces an "app" concept
  UCE does not have, makes every edit a global relink, turns the artifact
  cache into a build graph — webpack's worst traits without its benefits.
  Rejected. The file stays the unit.
- **Eager pre-loading of known units at worker warm-up.** Rejected; loading
  is strictly lazy, on first explicit call, preserving current semantics.

---

## 3. Architecture overview

### 3.1 Execution model

```
host process (linux_fastcgi, per worker)
│
├─ vendored wasm runtime (§10.1)
├─ unit artifact cache: one PIC .wasm per unit (replaces per-unit .so)
├─ loader (§6): dylink parsing, base allocation, GOT resolution,
│   symbol registry, ABI stamp check
├─ core snapshot: "core module, initialized" memory+table image
│
└─ per request: WORKSPACE
   ├─ linear memory (CoW-born from core snapshot)
   ├─ shared funcref table
   ├─ core module instance  (uce_lib + libc compiled to wasm)
   ├─ unit module instances (loaded lazily, on first call, incl. mid-request)
   └─ host handle table     (sqlite/mysql/file/socket handles + closers)
```

- **Workspace = request.** Born from the core snapshot, dropped at request
  end. Memory drop is the arena; handle-table drop is resource cleanup
  (this generalizes and replaces the per-connector
  `cleanup_*_connections()` pattern — RECOMMENDATIONS.md 1.7 / 5.1 become
  structurally unrepresentable).
- **One unit = one PIC wasm module.** Compile, cache, and invalidation
  granularity stay per-file. The `.uce → C++` translation pipeline is
  unchanged; only the compile target changes
  (`clang --target=wasm32-wasi -fPIC` + `wasm-ld -shared`).
- **Strictly lazy loading.** A unit's module is instantiated into a
  workspace the first time that workspace calls it — including mid-request.
  This is the wasm equivalent of today's compile-and-`dlopen`-on-first-hit
  and requires no restart, no fallback path. Placement memoization
  (deterministic bases so repeat instantiation is cheaper) is a permitted
  optimization; it must not change the loading policy.
- **In-flight isolation.** Module versions are immutable; a recompiled unit
  becomes a new module. Running workspaces keep what they loaded; new
  workspaces get the new version. Old modules are dropped when unreferenced
  (safe unload — impossible with `dlclose`).

### 3.2 Memory model

- **One heap, one allocator, one DTree implementation** — all owned by the
  core module. Unit modules *import* `malloc`/`free`/runtime symbols via GOT;
  the loader **rejects any unit module that defines rather than imports
  them** (two allocators on one heap is the one fatal misconfiguration).
- **Bump allocator option.** Because the workspace heap is dropped wholesale,
  the core's allocator may be a bump allocator with no-op free — the
  `_scratchpad.cpp` design, now correct by construction. Per-deployment
  flag; fallback is wasi-libc dlmalloc. Memory stats = heap pointer − base
  (replaces the tracking `operator new` in `types.h`).
- **Unit statics reset per request** (workspace is born from the core
  snapshot, which does not include unit data; unit data segments initialize
  at unit load within the workspace). This is *more* shared-nothing than
  today, where `.so` statics persist across requests within a worker.
  Cross-request state must use explicit host facilities (sessions, caches).
  **Breaking change — must be called out in docs and checked against the
  site/ tree during Phase 5.**

### 3.3 The host membrane

Exactly three currencies cross between host and workspace:

1. **scalars** (i32/i64/f64),
2. **byte buffers** (`ptr+len` into linear memory; inbound buffers are
   placed via the core's exported allocator),
3. **handles** (opaque `u32` indices into the per-workspace host handle
   table; each entry carries a closer callback).

Everything pointer-shaped stays on its own side. Hostcall surface budget:
30–60 functions (§5.1). Host errors return as error values; traps are
reserved for unit faults. Nothing throws across the membrane.

**DTrees cross the membrane only as the versioned wire encoding** (§5.3),
at exactly three sites: request context in (once), response out (once),
bulk I/O results in (per query, optional vs. cursor-style hostcalls).

### 3.4 DTree inside the workspace: no serialization, ever

Within a workspace, all modules share one address space, one toolchain, one
set of headers — the C/C++ ABI is intact across module boundaries. Therefore:

- A `DTree` is a pointer (an i32 offset into linear memory).
- `component(path, context)` resolves path → table index (host registry or
  guest-resident map) and `call_indirect`s, passing the context pointer.
  Reference semantics, mutation visibility, shared ob stack, working `ONCE`
  — identical to today.
- Function pointers are shared-table indices, valid across modules: virtual
  calls, `std::function` callbacks (`dtree_map` lambdas) work across units.
- Cost: one `call_indirect` (single-digit ns) + GOT loads for cross-module
  symbols — the same shape of overhead native PIC pays through the PLT/GOT,
  i.e. what dlopened `.so` units pay today.

Encode/decode is **not** part of internal component calls. It exists only at
the membrane (§3.3) and on the cross-instance plane (§4).

### 3.5 The DTree C ABI (load-bearing, build it first)

The stable contract of the workspace is a **C ABI**, not the C++ class:
the core exports `extern "C"` accessors over an opaque `uce_dtree*` (§5.2),
plus the string/ob/print helpers. C++ units may bypass it and use the class
directly (same headers, zero cost — a private fast path). Every other
workspace language uses the C surface.

This ABI is versioned: every unit artifact carries a custom section
(`uce.abi`: core ABI version + toolchain fingerprint); the loader refuses
stale units and triggers lazy recompilation (units are lazily compiled
anyway, so this costs nothing structurally).

**Phase 1 of the implementation plan is to introduce this C ABI in the
current native runtime** — it is useful immediately (plugin surface,
testability) and de-risks the rest.

---

## 4. Two component-call planes / multi-language support

The design stratifies languages by one question: *can the toolchain produce a
PIC linear-memory module that adopts a foreign allocator?*

**Plane A — workspace peers** (C++, C, Rust, Zig, …):
- Join the workspace as PIC modules importing core symbols.
- Must adopt the core allocator (Rust: `#[global_allocator]`; Zig: allocator
  parameter) and must not unwind across boundaries (`panic=abort` /
  catch-at-edge).
- Access DTrees through the C ABI: pointer semantics, no copies, ns-scale
  calls. Idiomatic wrappers per language (e.g. Rust `DTree<'request>` —
  the borrow checker enforces the arena invariant).

**Plane B — runtime-carrying languages** (JS, Python, Go, C#, …):
- Their GC/runtime owns its memory; they run as **separate instances**
  within the request and communicate through the host using the wire
  encoding and handles. Bindings choose per-access hostcalls or bulk
  subtree hydration into native dicts/objects — a tuning decision, not an
  architectural fork.

**The semantic rule (enforced by the loader, not by convention):**
cross-plane components do **not** receive the mutable context. They get an
explicit interface — props in (copied by definition), rendered output and
declared results back. Plane A keeps the full "here's the world, mutate it"
contract. A `component()` call must never silently change mutation semantics
based on the callee's implementation language.

The cross-instance call mechanism is shared by: Plane B units, and future
cross-trust-boundary components (multi-tenant). Serialization boundaries and
isolation boundaries are the same lines, by design.

---

## 5. ABI sketches (to be finalized in Phase 0/1)

### 5.1 Hostcall surface (grouped; target ≤ 60 functions)

```
request:   uce_host_ctx_read(buf) → len          // wire-encoded context, once
response:  uce_host_respond(status, hdrs_buf, body_buf)
           uce_host_stream_write(buf)            // chunked/streaming path
log:       uce_host_log(level, buf)
sqlite:    uce_host_sqlite_connect(path_buf) → handle | err
           uce_host_sqlite_query(handle, sql_buf, params_buf) → result_buf | err
           uce_host_sqlite_cursor_*(...)         // optional row-cursor variant
           uce_host_sqlite_insert_id/affected/error/disconnect(handle)
mysql:     (same shape; existing connector APIs are already handle-shaped)
files:     uce_host_file_read/write/stat/list(path_buf, ...)   // policy-gated
session:   uce_host_session_get/set(key_buf, val_buf)
http:      uce_host_http_request(req_buf) → handle/result_buf  // outbound
misc:      uce_host_time(), uce_host_random(buf), uce_host_env(key_buf)
loader:    uce_host_component_resolve(path_buf) → table_index  // may load (§6)
ws:        uce_host_ws_send(buf), event delivery via render entry re-invocation
```

Conventions: all errors as result codes + `uce_host_last_error(buf)`;
inbound buffers placed via the core's exported `uce_alloc`; no hostcall
traps on bad input (clamp/error instead).

### 5.2 DTree C ABI (core exports; sketch)

```c
typedef struct uce_dtree uce_dtree;                 // opaque; workspace-owned

uce_dtree*  uce_dtree_root(void);                   // request context
uce_dtree*  uce_dtree_get(uce_dtree*, const char* key, size_t klen); // create-on-write
uce_dtree*  uce_dtree_find(uce_dtree*, const char* key, size_t klen); // NULL if absent
const char* uce_dtree_value(uce_dtree*, size_t* len);
void        uce_dtree_set_value(uce_dtree*, const char* v, size_t vlen);
size_t      uce_dtree_count(uce_dtree*);
int         uce_dtree_is_list(uce_dtree*);
/* iteration */
uce_dtree_iter uce_dtree_iter_begin(uce_dtree*);
int         uce_dtree_iter_next(uce_dtree*, uce_dtree_iter*,
                                const char** key, size_t* klen, uce_dtree** child);
/* encode/decode at the membrane */
size_t      uce_dtree_encode(uce_dtree*, char* buf, size_t cap);   // → UCEB1
uce_dtree*  uce_dtree_decode(const char* buf, size_t len);
/* ob / print / helpers: uce_print, uce_ob_start, uce_ob_get_close,
   uce_html_escape, uce_json_encode, ... (mirror uce_lib surface) */
```

No unwinding across this surface; C++ exceptions are caught at the edge and
surfaced as error returns where fallible.

### 5.3 Wire encoding "UCEB1" (membrane + cross-instance only)

Length-prefixed binary tree; **not** JSON. Sketch (finalize against DTree's
actual fields — value + ordered children):

```
node  := value children
value := varint len, bytes (utf-8)
children := varint count, count × ( key: varint len + bytes, node )
flags := one leading byte per node reserved (bit0: is_list hint)
header := "UCEB" u8 version
```

This encoding is a **versioned protocol** from day one (header byte). It is
the Plane B contract and the membrane format; internal calls never see it.

### 5.4 Unit module contract

```
custom sections:  dylink.0 (standard), uce.abi { abi_version, toolchain_id }
imports:          env.memory, env.__indirect_function_table,
                  env.__memory_base, env.__table_base,
                  GOT.mem.* / GOT.func.* (resolved by loader),
                  core symbols (malloc, uce_dtree_*, uce_print, ...)
exports:          uce_unit_setup, uce_unit_render,
                  uce_unit_component, uce_unit_websocket
                  (same roles as today's UCE_SETUP/RENDER/COMPONENT/WEBSOCKET
                   dlsym symbols in compiler.cpp)
forbidden:        defining malloc/free/operator new, own memory, start fn
                  with side effects beyond data init
```

---

## 6. The loader (host-side, custom, load-bearing)

Owned code, ~1–2k lines, vendored-runtime-adjacent. Reference logic:
Emscripten's dylink loader (the ABI is the stable, battle-tested part; the
server-side loader is what doesn't exist off the shelf).

Per `load(unit)` into a workspace:

1. Fetch compiled module from artifact cache (compile on miss — today's
   lazy-compile path, retargeted).
2. Verify `uce.abi` stamp against the core; on mismatch, recompile unit.
3. Verify import discipline (no allocator/runtime definitions; §3.2).
4. Parse `dylink.0`: data size/alignment, table slots needed.
5. Allocate `__memory_base` (bump within workspace data region) and
   `__table_base` (append to shared table).
6. Instantiate with bases; resolve `GOT.*` imports against the workspace
   symbol registry (core symbols + previously loaded units); register the
   unit's exports.
7. Register entry points in the path → table-index dispatch map.

`uce_host_component_resolve(path)` consults the dispatch map and calls
`load()` on miss — this is how lazy, programmatic, mid-request loading works
with no special cases.

Placement memoization (optional, later): record each unit's first-assigned
bases; reuse across workspaces so instantiation is cheaper and snapshot
growth (below) stays consistent. Does not change the lazy policy.

**Core snapshot:** the only pre-built state is "core module, initialized" —
memory bytes + table state captured once per core build. Workspaces are born
from it via CoW (`mmap(MAP_PRIVATE)` of the snapshot image; the host owns
the Memory object, so OS-level CoW is available). No units are pre-fed.

---

## 7. Request lifecycle (replaces the native flow in linux_fastcgi.cpp)

```
1. accept request
2. workspace = birth_from_core_snapshot()          (CoW, ~µs)
3. write wire-encoded context into workspace; core decodes → context DTree
4. resolve entry unit (load on first call); call uce_unit_render(ctx_ptr)
5. component(path) inside guest → resolve hostcall → (lazy load) →
   call_indirect — reference semantics throughout
6. I/O via hostcalls; resources land in the workspace handle table
7. on return: encode response/headers out; write FastCGI response
   on trap: defined error → render error page with guest stack trace;
   workspace state is irrelevant because…
8. drop workspace: linear memory gone (arena), handle table closed
   (generalized resource cleanup), instances released
```

CPU limit: epoch/fuel interruption → same path as trap.
Memory limit: linear memory max → allocation failure / trap → same path.

---

## 8. What carries over unchanged

- The `.uce → C++` translation, parser, and page semantics.
- The lazy compile-on-first-request model and per-unit artifact caching
  (different artifact format).
- The host-side connectors (sqlite/mysql) — already handle-shaped APIs; they
  move behind hostcalls with the same `.uce`-visible signatures.
- The site tree, docs, demo, and the network test suite
  (`tests/run_network_tests.py`) — which becomes the parity harness (§9).
- nginx/FastCGI front-end integration, worker model, websocket event flow
  (events re-enter via the websocket entry point; note: a workspace-per-event
  or workspace-per-connection decision is an open question, §11).

---

## 9. Implementation plan

Phases are sequential; each has an exit criterion. No phase except 0 and 2's
scaffolding produces throwaway work.

**Phase 0 — toolchain & runtime spike (timeboxed).**
Validate: wasi-sdk `-fPIC` + `wasm-ld -shared` on a representative generated
unit; cross-module C++ calls with shared memory/table; exceptions decision
(wasm EH vs. error-code discipline at unit boundaries — pick one, record it);
vendored runtime selection. Candidates: **WAMR** (C, small, designed for
embedding, easiest to vendor and patch — fits the project's vendoring
practice) vs. **Wasmtime** (fastest, best AOT/CoW machinery, Rust — heavier
to vendor/patch). Selection criteria: imported-memory + shared-table support,
AOT artifact quality, patchability. Exit: a two-module (core stub + unit
stub) hello-world linked at runtime by a minimal loader, in the chosen
vendored runtime.

**Phase 1 — DTree C ABI in the native runtime.**
Introduce `uce_dtree_*` (§5.2) and the UCEB1 codec in `src/lib/`, used
natively. Zero wasm dependency; immediately testable; freezes the contract
everything else builds on. Exit: codec round-trip + accessor tests in the
existing suite; ABI doc checked in.

**Phase 2 — core module + membrane.**
Compile `uce_lib` (+ wasi-libc) to wasm as the core module; implement the
hostcall surface (§5.1) in the host; temporary scaffolding allowed: one
statically-linked unit + core to validate codegen and membrane without the
loader. Exit: one real `.uce` page (e.g. `site/tests/core.uce`) renders
correctly through the membrane. Scaffolding is marked throwaway.

**Phase 3 — the loader + workspace.**
Implement §6 in full: dylink parsing, base allocation, GOT resolution,
ABI/import verification, lazy mid-request loading, path dispatch. Per-request
workspace birth/drop (plain memcpy birth is fine here; CoW is Phase 4).
Exit: the uce-starter renders end-to-end with components loading lazily;
`tests/run_network_tests.py --match starter` passes against the wasm worker.

**Phase 4 — production mechanics.**
Core snapshot + CoW birth; bump-allocator flag; epoch/memory limits; trap →
error-page path with guest stack traces (this supersedes the
signal/longjmp machinery and closes RECOMMENDATIONS.md 1.5 structurally);
handle-table cleanup (closes 1.7/5.1); artifact/ABI versioning end-to-end.
Exit: kill-tests (deliberate null-deref page, infinite-loop page, OOM page)
produce clean error pages and an unharmed worker.

**Phase 5 — parity & performance.**
Full network suite green on the wasm worker; differential native-vs-wasm runs
on the site tree; audit `site/` for cross-request-static reliance (§3.2
breaking change); benchmark suite (template-heavy page, sqlite page,
component-heavy starter page) with budgets: ≤2× native page latency,
workspace birth ≤100µs, internal component call overhead within 10× native
call cost. Exit: numbers published in this document; go/no-go for default
backend.

**Phase 6 — second plane (deferred until wanted).**
Cross-instance call mechanism (props-in/output-out, UCEB1), first Plane B
language binding, loader enforcement of the cross-plane context rule (§4).
Plane A second language (Rust) as the cheaper first polyglot proof.

The native `.so` backend remains in-tree and selectable until Phase 5's
go/no-go; both backends share the Phase 1 C ABI.

---

## 10. Risks & mitigations

| Risk | Mitigation |
|---|---|
| wasi-sdk PIC / shared-library maturity (least-trodden toolchain path) | Phase 0 spike before any commitment; pin toolchain versions; statically link libc into the core and export from there (avoid shared wasi-libc entirely) |
| C++ exceptions × PIC × wasm EH | Phase 0 decision point; fallback is error-code discipline at unit entry points (units already have a uniform entry shape) |
| Custom loader correctness (GOT, bases, relocation) | Small, contained (~1–2k lines); crib logic from Emscripten's reference loader; fuzz with adversarial modules; loader rejects > loader guesses |
| Vendored runtime patches drift from upstream | Same practice as vendored SQLite: provenance + patch files under `docs/patches/`; pin upstream tag; tests gate upgrades |
| Performance regression beyond budget | Phase 5 gates; bump allocator and placement memoization in reserve; native backend retained |
| Multi-module DWARF / debugging story | Trap stack traces cover the production case (better than today); accept weaker interactive debugging; keep native backend for local deep-debugging |
| Unit-statics semantic change breaks existing pages | Phase 5 audit of `site/`; documented migration note; host-side cache facility if a real need surfaces |

## 11. Open questions

1. **Exceptions:** wasm EH or error codes at unit boundaries? (Phase 0.)
2. **WebSocket granularity:** workspace per event (pure arena, statics reset
   per event) or per connection (state across events, bounded lifetime)?
   Leaning per-event for consistency; needs a look at current WS page usage.
3. **UCEB1 final layout** vs. DTree's actual field set (value/children/list
   flag) — finalize in Phase 1.
4. **Cursor vs. bulk** as the default for `sqlite_query` results at the
   membrane (offer both; pick the default after Phase 5 benchmarks).
5. **Streaming output:** today's ob model buffers; does the membrane expose
   `uce_host_stream_write` from day one or post-MVP?
6. **Core snapshot rebuild cadence** once placement memoization lands
   (dead-base reclamation policy).

## 12. Summary

The module is the **unit** (file-grained, lazily compiled, lazily loaded —
including mid-request). The instance set is the **workspace** (one per
request; shared memory + table; born from a core-only CoW snapshot; dropped
wholesale — the arena, done right). The contract is the **DTree C ABI**
inside the workspace (pointer semantics, no serialization) and the **UCEB1
wire encoding + handles** at every true address-space boundary (host
membrane, Plane B languages, future trust boundaries). Serialization
boundaries and isolation boundaries are the same lines; component calls stay
function calls; and the file stays the unit.
