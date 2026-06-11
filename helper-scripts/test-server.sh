#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════
# JIC server tests — exercises a RUNNING server (docker compose up).
#
#   JIC_URL=http://localhost:8080 ./helper-scripts/test-server.sh
#
# Degraded mode (no GGUF models) is a valid deployment state: /query is
# then expected to answer 503, and that is asserted instead of skipped.
# ══════════════════════════════════════════════════════════════════════
set -uo pipefail

URL="${JIC_URL:-http://localhost:8080}"
WAIT_SECS="${JIC_WAIT_SECS:-90}"

PASS=0
FAIL=0
ok()  { PASS=$((PASS + 1)); echo "  ✅ $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  ❌ $1"; }

echo "🧪 JIC server tests against $URL"
echo "================================"

# ── 0. Wait for the server ───────────────────────────────────────────
echo ""
echo "⏳ Waiting for server (max ${WAIT_SECS}s)"
up=0
for _ in $(seq 1 "$WAIT_SECS"); do
    code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 "$URL/status" || true)
    if [[ "$code" == "200" ]]; then up=1; break; fi
    sleep 1
done
if [[ $up -eq 1 ]]; then
    ok "server is up"
else
    bad "server did not come up within ${WAIT_SECS}s"
    echo "================================"
    echo "  $PASS passed, $FAIL failed"
    exit 1
fi

# ── 1. Status endpoint ───────────────────────────────────────────────
echo ""
echo "📊 /status"
status_json=$(curl -s "$URL/status")
for field in documents_indexed files_processed llm_loaded embeddings_loaded version uptime_seconds; do
    if printf '%s' "$status_json" | grep -q "\"$field\""; then
        ok "status has $field"
    else
        bad "status missing $field (got: ${status_json:0:160})"
    fi
done
llm_loaded=$(printf '%s' "$status_json" | grep -o '"llm_loaded":[a-z]*' | cut -d: -f2)

# ── 2. Static UI ─────────────────────────────────────────────────────
echo ""
echo "🖥  Static UI"
home=$(curl -s "$URL/")
if printf '%s' "$home" | grep -qi "Just in Case"; then ok "/ serves the JIC UI"; else bad "/ does not look like the JIC UI"; fi

for asset in style.css app.js assets/favicon.svg assets/logo.svg; do
    code=$(curl -s -o /dev/null -w '%{http_code}' "$URL/$asset")
    if [[ "$code" == "200" ]]; then ok "/$asset → 200"; else bad "/$asset → $code"; fi
done

code=$(curl -s -o /dev/null -w '%{http_code}' "$URL/definitely-not-a-real-page")
if [[ "$code" == "404" ]]; then ok "unknown path → 404"; else bad "unknown path → $code (expected 404)"; fi

# ── 3. Security headers ──────────────────────────────────────────────
echo ""
echo "🔒 Security headers"
hdrs=$(curl -sI "$URL/")
printf '%s' "$hdrs" | grep -qi '^content-security-policy:' \
    && ok "CSP present on HTML" || bad "CSP missing on HTML"
printf '%s' "$hdrs" | grep -qi '^x-content-type-options: *nosniff' \
    && ok "X-Content-Type-Options: nosniff" || bad "X-Content-Type-Options missing"
printf '%s' "$hdrs" | grep -qi '^x-frame-options' \
    && ok "X-Frame-Options present" || bad "X-Frame-Options missing"
if printf '%s' "$hdrs" | grep -qi '^access-control-allow-origin'; then
    bad "CORS is open by default (Access-Control-Allow-Origin present)"
else
    ok "CORS disabled by default"
fi

api_hdrs=$(curl -sI "$URL/status")
if printf '%s' "$api_hdrs" | grep -qi '^content-security-policy:'; then
    bad "CSP leaked onto JSON responses (breaks in-browser PDF viewing policy)"
else
    ok "CSP scoped to HTML only"
fi

# ── 4. /api/library ──────────────────────────────────────────────────
echo ""
echo "📚 /api/library"
lib=$(curl -s "$URL/api/library")
printf '%s' "$lib" | grep -q '"files"' && ok "library has files[]" || bad "library missing files[] (got: ${lib:0:120})"
printf '%s' "$lib" | grep -q '"total_chunks"' && ok "library has total_chunks" || bad "library missing total_chunks"

# ── 5. /query validation ─────────────────────────────────────────────
echo ""
echo "💬 /query validation"

if [[ "$llm_loaded" == "false" ]]; then
    # Degraded mode: everything answers 503 before validation
    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$URL/query" \
        -H 'Content-Type: application/json' -d '{"query":"How do I purify water?"}')
    [[ "$code" == "503" ]] && ok "degraded mode: /query → 503" \
                           || bad "degraded mode: /query → $code (expected 503)"
else
    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$URL/query" \
        -H 'Content-Type: application/json' -d 'this is not json')
    [[ "$code" == "400" ]] && ok "invalid JSON → 400" || bad "invalid JSON → $code"

    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$URL/query" \
        -H 'Content-Type: application/json' -d '{"not_query": 1}')
    [[ "$code" == "400" ]] && ok "missing query field → 400" || bad "missing query → $code"

    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$URL/query" \
        -H 'Content-Type: application/json' \
        -d "{\"query\":\"$(printf 'a%.0s' $(seq 1 9000))\"}")
    [[ "$code" == "400" ]] && ok "oversized query → 400" || bad "oversized query → $code"

    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$URL/query" \
        -H 'Content-Type: application/json' \
        -d '{"query":"hi","conversation_id":"../../etc/passwd"}')
    [[ "$code" == "400" ]] && ok "malformed conversation_id → 400" || bad "malformed conversation_id → $code"

    echo ""
    echo "💬 /query end-to-end (this loads the LLM — may take a while)"
    resp=$(curl -s --max-time 600 -X POST "$URL/query" \
        -H 'Content-Type: application/json' \
        -d '{"query":"In one sentence, how can drinking water be made safe?"}')
    if printf '%s' "$resp" | grep -q '"answer"'; then
        ok "query returned an answer"
        echo "      ↳ $(printf '%s' "$resp" | tr -d '\n' | sed 's/.*"answer":"//;s/".*//' | head -c 140)…"
    else
        bad "query returned no answer (got: ${resp:0:160})"
    fi
fi

# ── Summary ──────────────────────────────────────────────────────────
echo ""
echo "================================"
echo "  $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
