# UCE Starter

`site/examples/uce-starter/` is a UCE-native port of the PHP `web-app-starter/`.

It keeps the PHP starter around as reference while mirroring the same broad structure:

- `index.uce` as the front controller
- `views/` for routed page content
- `components/` for reusable UI building blocks
- `themes/`, `js/`, and `img/` for static assets

This port intentionally leans on UCE's component layer rather than treating components as a compatibility shim. The page shell, nav/footer chrome, theme switcher, dashboard blocks, workspace primitives, and marketing sections are all rendered through `component()`.

The example uses query-string routing in the same style as the PHP starter:

- `index.uce`
- `index.uce?page1`
- `index.uce?themes&theme=portal-dark`
- `index.uce?workspace/projects`

The demo account pages use a small file-backed user store under `/tmp/uce-starter-data/` with session-based login state.
