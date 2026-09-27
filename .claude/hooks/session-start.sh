#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════
# JIC — SessionStart hook for Claude Code on the web.
#
# Prepares the checks an agent can actually run in a cloud session. It
# deliberately does NOT build the product: jic-server links llama.cpp,
# MuPDF and sqlite-vec, which is a ~20-minute cold compile and belongs in
# `docker compose up --build`, not in session startup.
#
# What it does prepare:
#   tests/unit      — header-only tests; fetches nlohmann/json + cpp-httplib
#   tests/container — Playwright harness (browser is preinstalled)
#   canon lint      — probes reachability and records the verdict, because
#                     the gate lives in GitHub Packages and needs a token
#                     that a cloud session usually does not carry.
#
# Local runs are untouched — the hook exits immediately unless it is a
# remote (web) session.
# ══════════════════════════════════════════════════════════════════════
set -uo pipefail

# Web sessions only. A developer's own machine is already set up, and this
# hook must never surprise one by mutating its state.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

ROOT="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
cd "$ROOT" || exit 1

note() { echo "  ✅ $1"; }
warn() { echo "  ⚠️  $1"; }

echo "🔧 JIC session setup"
echo "===================="

# ── 1. Header-only unit tests ────────────────────────────────────────
# The Makefile downloads json.hpp and httplib.h as prerequisites rather
# than vendoring them. Building here means the deps are cached in the
# container image and `make -C tests/unit` runs offline afterwards.
echo ""
echo "🧪 Unit test dependencies"
if make -C tests/unit test_text_utils test_telemetry test_telemetry_scrub test_kiwix_parse >/tmp/jic-hook-unit.log 2>&1; then
    note "tests/unit compiled (deps cached in .deps/)"
else
    warn "tests/unit did not build — see /tmp/jic-hook-unit.log"
    tail -5 /tmp/jic-hook-unit.log | sed 's/^/      /'
fi

# ── 2. Container screenshot harness ──────────────────────────────────
# Chromium ships with the image; PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD stops
# the postinstall from re-fetching several hundred megabytes. `npm install`
# (not `ci`) so the resolved tree is reused from the cached layer.
echo ""
echo "🎭 Container test harness"
if [ -f tests/container/package.json ]; then
    if (cd tests/container && PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm install --no-audit --no-fund >/tmp/jic-hook-npm.log 2>&1); then
        note "tests/container node_modules installed"
    else
        warn "npm install failed in tests/container — see /tmp/jic-hook-npm.log"
    fi
fi

# ── 3. Style canon lint reachability ─────────────────────────────────
# CI runs `ci-lint-canon` from @companionintelligence/config, published to
# GitHub Packages. A cloud session's token normally lacks read:packages, so
# the probe records the verdict instead of leaving the next agent to burn a
# few minutes finding out. scripts/vendor-brand.sh --check is the gate that
# always works — it verifies the vendored bundle against CI-Common's
# published SHA256SUMS with no registry involved.
echo ""
echo "🎨 Style canon lint"
CANON_AVAILABLE=0
if [ -n "${NODE_AUTH_TOKEN:-}" ] || [ -n "${GITHUB_TOKEN:-}" ]; then
    token="${NODE_AUTH_TOKEN:-${GITHUB_TOKEN:-}}"
    probe_rc="$(mktemp)"
    printf '@companionintelligence:registry=https://npm.pkg.github.com/\n//npm.pkg.github.com/:_authToken=%s\n' "$token" > "$probe_rc"
    if NPM_CONFIG_USERCONFIG="$probe_rc" npm view @companionintelligence/config version >/dev/null 2>&1; then
        # Keep the credential out of the repo: a user-level npmrc, never a
        # committed one.
        printf '@companionintelligence:registry=https://npm.pkg.github.com/\n//npm.pkg.github.com/:_authToken=%s\n' "$token" >> "$HOME/.npmrc"
        CANON_AVAILABLE=1
        note "canon lint reachable (scope configured in ~/.npmrc)"
    else
        warn "canon lint unreachable — token lacks read:packages"
        note "use scripts/vendor-brand.sh --check instead (works offline)"
    fi
    rm -f "$probe_rc"
else
    warn "no GitHub token in env — canon lint unavailable"
    note "use scripts/vendor-brand.sh --check instead (works offline)"
fi

# ── 4. Session environment ───────────────────────────────────────────
if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
    {
        # Chromium is preinstalled in the image; point Playwright at it and
        # stop any later npm install from downloading its own copy.
        echo "export PLAYWRIGHT_BROWSERS_PATH=${PLAYWRIGHT_BROWSERS_PATH:-/opt/pw-browsers}"
        echo "export PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1"
        # Read by an agent deciding which brand gate to run.
        echo "export JIC_CANON_LINT_AVAILABLE=${CANON_AVAILABLE}"
    } >> "$CLAUDE_ENV_FILE"
    note "session env written"
fi

echo ""
echo "Checks available in this session:"
echo "  ./helper-scripts/test-config.sh     static config tests (no Docker)"
echo "  make -C tests/unit                  header-only unit tests"
echo "  scripts/vendor-brand.sh --check     brand/token conformance"
if [ "$CANON_AVAILABLE" = "1" ]; then
    echo "  npx --package @companionintelligence/config ci-lint-canon . \\"
    echo "      --tokens public/assets/brand/tokens/tokens.json"
fi
echo ""
echo "Full build + server tests need Docker:"
echo "  docker compose up --build -d && ./helper-scripts/test-server.sh"

exit 0
