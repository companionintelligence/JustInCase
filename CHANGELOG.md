# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed — Design system

The `public/` web UI was aligned to the Companion Intelligence canonical design
system. All changes are front-end only (`index.html`, `app.js`, `style.css`); the
C++ `jic-server` and its API contract (`GET /status`, `GET /api/library`,
`POST /query`) are unchanged.

- **Canonical teal primary.** Adopted the CI canonical teal as the accent/primary
  color — `#0f717a` in the light theme and `#abd4d8` in the dark theme (dark is the
  default for low-light field use). The theme toggle persists to `localStorage`.
- **Typography.** Self-hosted, offline brand fonts matching the
  [ci.computer](https://ci.computer) site — **Abel** for display/sans and
  **Source Code Pro** for mono — bundled as WOFF2 under `public/assets/fonts/`
  (no CDN, OFL-licensed).
- **Enriched design tokens (v0.2.0).** Expanded the CSS custom-property token set
  to mirror the CI canon (`@companionintelligence/tokens` / CI-Hub `globals.css`),
  inlined directly into `style.css`: a full teal ramp (`--teal-50`…`--teal-950`),
  aqua/mint accent hues, semantic status colors (`--success`, `--warning`,
  `--info` with foregrounds), brand/hero/aurora gradients, a radius scale
  (`--radius-sm`…`--radius-2xl`), and an elevation/shadow scale — all defined per
  theme (light + dark).
- **Design-canon lint gate.** Added `scripts/lint-canon.mjs` (zero-dependency,
  Node built-ins only) and the `style-canon-lint.yml` GitHub Actions workflow,
  which fail the build if any resolvable `--primary` value drifts off the CI teal
  (in any color notation — hex, rgb, hsl, oklch/oklab) and warn on hardcoded hex
  in component source.
- **Refreshed UI screenshots.** Regenerated the light and dark UI captures in
  `docs/ui/` (welcome, grounded-answer/sources, degraded state, mobile drawer) as
  real headless renders of `public/index.html`, and referenced them from the
  README.
- Completed the dark-theme gradient token set by defining `--gradient-brand` and
  `--gradient-hero` in the default (`:root`) theme, matching the light theme so the
  enriched gradient tokens are available in both.
