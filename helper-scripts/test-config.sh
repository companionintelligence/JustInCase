#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════
# JIC static configuration tests — no network, no Docker daemon needed.
# Validates that the repo is internally consistent before any build.
# ══════════════════════════════════════════════════════════════════════
set -uo pipefail

cd "$(dirname "$0")/.."

PASS=0
FAIL=0

ok()   { PASS=$((PASS + 1)); echo "  ✅ $1"; }
bad()  { FAIL=$((FAIL + 1)); echo "  ❌ $1"; }
check() { # check <description> <command...>
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then ok "$desc"; else bad "$desc"; fi
}

echo "🔧 JIC configuration tests"
echo "=========================="

# ── 1. Required files ────────────────────────────────────────────────
echo ""
echo "📁 Required files"
for f in Dockerfile docker-compose.yml CMakeLists.txt sources.yaml \
         src/server.cpp src/ingestion.cpp src/config.h src/sqlite_vec_index.h \
         public/index.html public/style.css public/app.js \
         public/assets/logo.svg public/assets/favicon.svg \
         helper-scripts/fetch-models.sh helper-scripts/fetch-source-data.sh; do
    check "$f exists" test -f "$f"
done

# ── 2. Shell script syntax ───────────────────────────────────────────
echo ""
echo "📜 Shell script syntax (bash -n)"
for s in helper-scripts/*.sh; do
    check "$s parses" bash -n "$s"
done

# ── 3. Source manifest ───────────────────────────────────────────────
echo ""
echo "📚 Source manifest"
if ./helper-scripts/fetch-source-data.sh --validate > /tmp/jic-manifest-lint.txt 2>&1; then
    ok "sources.yaml validates ($(grep -c 'entries' /tmp/jic-manifest-lint.txt >/dev/null 2>&1; tail -1 /tmp/jic-manifest-lint.txt))"
else
    bad "sources.yaml validation failed:"
    sed 's/^/      /' /tmp/jic-manifest-lint.txt
fi

# ── 4. Compose ↔ scripts consistency ────────────────────────────────
echo ""
echo "🔗 Configuration consistency"

compose_llm=$(grep -oE 'LLM_GGUF_FILE: *[^ ]+' docker-compose.yml | head -1 | awk '{print $2}')
script_llm=$(grep -oE 'LLM_GGUF_FILE:-[^}]+' helper-scripts/fetch-models.sh | head -1 | cut -d- -f2- | sed 's/^//;s/}$//')
script_llm=${script_llm#LLM_GGUF_FILE:-}
if [[ -n "$compose_llm" && "$compose_llm" == "${script_llm}" ]]; then
    ok "LLM model file consistent: $compose_llm"
else
    bad "LLM model mismatch: compose='$compose_llm' fetch-models.sh='$script_llm'"
fi

compose_emb=$(grep -oE 'EMBEDDING_GGUF_FILE: *[^ ]+' docker-compose.yml | head -1 | awk '{print $2}')
script_emb=$(grep -oE 'EMBEDDING_GGUF_FILE:-[^}]+' helper-scripts/fetch-models.sh | head -1)
script_emb=${script_emb#EMBEDDING_GGUF_FILE:-}
if [[ -n "$compose_emb" && "$compose_emb" == "$script_emb" ]]; then
    ok "Embedding model file consistent: $compose_emb"
else
    bad "Embedding model mismatch: compose='$compose_emb' fetch-models.sh='$script_emb'"
fi

src_default_llm=$(grep -oE '"LLM_GGUF_FILE", *"[^"]+"' src/config.h | sed 's/.*", *"//;s/"//')
if [[ "$src_default_llm" == "$compose_llm" ]]; then
    ok "config.h default LLM matches compose"
else
    bad "config.h default LLM ('$src_default_llm') != compose ('$compose_llm')"
fi

# ── 5. Content stays out of the image ────────────────────────────────
echo ""
echo "📦 Image hygiene"
check ".dockerignore excludes public/sources/" grep -q '^public/sources/' .dockerignore
check ".dockerignore re-includes the content fetcher" grep -q '^!helper-scripts/fetch-source-data.sh' .dockerignore
check "Dockerfile runs as non-root user" grep -q '^USER jic' Dockerfile
check "compose uses a named sources volume" grep -q 'jic-sources:/app/public/sources' docker-compose.yml
check "compose uses a named data volume" grep -q 'jic-data:/app/data' docker-compose.yml

# ── 6. UI wiring ─────────────────────────────────────────────────────
echo ""
echo "🎨 UI wiring"
check "index.html loads app.js" grep -q 'src="app.js"' public/index.html
check "index.html loads style.css" grep -q 'href="style.css"' public/index.html
check "index.html uses the SVG favicon" grep -q 'assets/favicon.svg' public/index.html
check "no inline event handlers (CSP)" bash -c '! grep -qE "on(click|keydown|load)=" public/index.html'
if command -v node >/dev/null 2>&1; then
    check "app.js parses (node --check)" node --check public/app.js
fi

# ── 7. Compose file validity (when docker is available) ──────────────
echo ""
echo "🐳 Compose validity"
if docker compose version >/dev/null 2>&1; then
    check "docker compose config is valid" docker compose config -q
else
    echo "  ⏭️  docker not available — skipped"
fi

# ── Summary ──────────────────────────────────────────────────────────
echo ""
echo "=========================="
echo "  $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
