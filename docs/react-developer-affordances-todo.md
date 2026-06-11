# React Developer Affordances Todo

## Objective

Add practical value for developers coming from React frameworks while preserving UCE's server-first C++ model. Defer component syntax/children work, avoid global head/assets/islands in the runtime, and focus on function-library data helpers, diagnostics, docs, examples, demos, and a starter-local router with starter-local asset/island components.

## Success Criteria

- [x] Function library has useful collection/data-shaping helpers with docs and tests.
- [x] Compile/runtime diagnostics are more helpful, especially for generated-code and common preprocessor mistakes.
- [x] Docs include a concise React/Next/Remix orientation guide.
- [x] Starter example uses a centralized hierarchical/file-based router in `index.uce` efficiently.
- [x] Starter-local asset/island affordances live as component handlers in the starter, not global runtime APIs.
- [x] Network tests and relevant build checks pass on `k-uce`.

## Current State

- Status: complete
- Last updated: 2026-05-28
- Source of truth: `/root/mount_ssh/k-uce-root-htdocs-uce`
- Runtime/live target: `k-uce:/Code/uce.openfu.com/uce`; rebuilt and restarted `uce.service` on `k-uce`.

## Goal Tree

Legend: `[ ]` not started, `[~]` in progress, `[x]` done, `[!]` blocked, `[-]` superseded

- [x] G1: Add collection/data helpers to function library
  - Why: React-framework developers routinely shape arrays/objects near render code.
  - Done when: helpers are declared, implemented, documented, and covered by tests.
  - Verify: build plus focused site/network tests.
  - [x] G1.1: Identify current `StringList`/`DTree` idioms and choose helper surface.
  - [x] G1.2: Implement minimal high-value helpers without broad template complexity.
  - [x] G1.3: Add docs and examples for helpers.
  - [x] G1.4: Add/extend tests.
- [x] G2: Improve developer diagnostics
  - Why: React frameworks win by making failures easy to act on.
  - Done when: compile/runtime error output includes actionable context and docs mention debugging flow.
  - Verify: intentional broken page surfaces improved message.
  - [x] G2.1: Inspect current compiler/runtime error rendering path.
  - [x] G2.2: Add source excerpt / generated path / common-hint text where appropriate.
  - [x] G2.3: Document diagnostics.
- [x] G3: Add React/Next/Remix orientation docs
  - Why: mapping familiar concepts reduces onboarding cost without adding syntax.
  - Done when: docs page exists and is linked from docs/README/demo surfaces.
  - Verify: docs page renders live.
- [x] G4: Starter-local router and starter affordances
  - Why: User specifically wants hierarchical/file-based routing beautifully in starter `index.uce`.
  - Done when: starter routes go through a central router in `index.uce`, and starter-local asset/island component handlers exist and are used where sensible.
  - Verify: key starter routes render 200.
  - [x] G4.1: Inspect current starter routing.
  - [x] G4.2: Refactor to clear route table / hierarchical file resolution in `index.uce`.
  - [x] G4.3: Add starter-local `COMPONENT:asset` / `COMPONENT:island` style handlers in one unit.
  - [x] G4.4: Use them efficiently in starter pages/layout.
- [x] G5: Demos and examples
  - Why: Affordances must be visible to developers, not hidden in APIs.
  - Done when: docs/demo/tests expose examples.
  - Verify: demo URLs return 200 and tests pass.
- [x] G6: Verification and project docs
  - Done when: build/test commands are run on `k-uce`, project notes updated, and adversarial review completed.

## Execution Queue

Complete.

## Decisions

- 2026-05-28: Defer component children/slots and JSX-like preprocessor syntax.
- 2026-05-28: Do not add global runtime head/assets/islands APIs; implement asset/island as starter-local components.
- 2026-05-28: Do not add a generic runtime file-based-routing system; demonstrate hierarchical/file routing inside starter `index.uce`.
- 2026-05-28: Keep collection helpers explicit (`list_*`, `dtree_*`) instead of overloading generic names such as `map`/`sort`.

## Assumptions

- Current source-of-truth mount is live-editable; runtime validation requires SSH to `k-uce`.
- Existing site tests are the right place for function-library coverage.

## Blockers and Risks

- No current blockers.
- Future risk: if UCE grows a full parser or component-tag syntax, keep this pass's explicit component/router APIs as a stable lower-level fallback.

## Evidence and Verification Log

- 2026-05-28: Created plan after reviewing README, preprocessor docs, and function library headers.
- 2026-05-28: `ssh k-uce 'cd /Code/uce.openfu.com/uce && bash scripts/build_linux.sh'` succeeded.
- 2026-05-28: Restarted `uce.service` on `k-uce`.
- 2026-05-28: `tests/run_network_tests.py --match core` passed.
- 2026-05-28: Manual checks returned `200` for `/examples/uce-starter/index.uce`, `?dashboard`, `?workspace/projects`, `?themes`, `/demo/collections.uce`, `/doc/index.uce?p=coming_from_react`, `/doc/index.uce?p=list_map`, and `/doc/index.uce?p=dtree_group_by`.
- 2026-05-28: Full internal network suite passed, 25/25.
- 2026-05-28: Temporary broken `/tests/diagnostic-probe.uce` returned `500` with formatted `UCE compile error` diagnostics; source and cache artifacts were removed afterward.

## Change Log

- 2026-05-28: Created initial goal tree.
- 2026-05-28: Implemented helpers, diagnostics, docs, demo, starter router, starter-local web affordance components, tests, and validation.

## Follow-up Cleanup 2026-05-29

- Removed duplicate route cleanup from `starter_router_candidates()` because `app_make_route()` is the single normalization point for `l_path`.
- Weeded out nearby duplicate/obsolete starter code:
  - `starter_router_add_candidate(...)` now owns repeated candidate tree construction.
  - Removed unused `app_resolve_view()` / `starter_resolve_view()` after moving routing into starter `index.uce`.
  - Removed unused legacy registered asset rendering functions from `lib/app.uce`; registered assets now render through `components/theme/web_affordances.uce`.
  - `app_init()` now reuses `app_base_url(context)` instead of repeating base URL derivation.
  - `web_affordances.uce` now uses one `starter_render_asset_group(...)` loop for CSS and JS.
- Verification: rebuilt on `k-uce`, restarted `uce.service`, checked key starter routes, and ran `tests/run_network_tests.py --match core` successfully.

## Follow-up Routed Views 2026-05-29

- Changed starter route dispatch from `unit_render(...)` to `component(...)`.
- Converted all routed `site/examples/uce-starter/views/*.uce` files to `COMPONENT(Request& context)` rather than `RENDER(Request& context)` because they are central-router-only views.
- Updated the starter README and verified key starter routes. No service restart was required because only `.uce`/docs changed.

## Follow-up Canonical Starter URLs 2026-05-29

- Canonicalized starter self-links from `/examples/uce-starter/index.uce?...` to `/examples/uce-starter/?...` with `app_canonical_script_url(...)`.
- Updated the starter README to show canonical directory URLs.
- Touched the starter front controller to force the `#load`ed helper change into the cached generated unit.
- Verified canonical/direct starter routes and checked generated self-links. No service restart was required.

## Follow-up Not Found Component 2026-05-29

- Moved `starter_router_render_not_found` markup into `components/basic/notfound.uce`.
- Router now delegates 404 body rendering through `component("components/basic/notfound", props, context)`.
- Verified missing routes return `404` and normal dashboard route returns `200`. No service restart required.

## Follow-up Page Shell Component 2026-05-29

- Moved app page rendering into `themes/page.uce` as a component.
- Removed `app_render_page`, `app_theme_page_component`, and `starter_render_page` from `lib/app.uce`.
- Page template resolution now follows `context.call["app"]["page_type"]`: current theme first, common fallback second.
- Verified representative HTML routes and the JSON page-type fallback. No service restart required.

## Follow-up Deep Starter Context Cleanup 2026-05-29

- Removed repeated `starter_boot(context)` calls; root `index.uce` is the boot point.
- Removed `context.call["starter"]` duplicated state and JSON side-channel state.
- JSON routes now use only `context.call["app"]["page_type"]` plus normal captured output.
- Simplified `themes/page.uce` and `themes/common/page.json.uce` accordingly.
- Replaced `starter_*` alias helper usage with direct `app_*` helpers and removed alias wrappers except `StarterUser`.
- Verified key routes and core tests. No service restart required.

## Follow-up Route Context Flattening 2026-05-29

- Flattened `context.call["app"]["route"]` to `context.call["route"]`.
- Moved former `context.call["app"]["router"]` metadata into `context.call["route"]`.
- Verified representative starter HTML, 404, and JSON routes. No service restart required.
