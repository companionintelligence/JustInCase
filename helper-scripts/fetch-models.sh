#!/bin/bash
set -euo pipefail

echo "═══════════════════════════════════════════"
echo "  JIC — Model Download"
echo "═══════════════════════════════════════════"
echo ""
echo "Docker will NOT download models at runtime."
echo "You must place GGUF files in ./gguf_models/ first."
echo ""

# ── Configurable via environment ─────────────────────────────────────
LLM_FILE="${LLM_GGUF_FILE:-Llama-3.2-3B-Instruct-Q4_K_M.gguf}"
LLM_REPO="${LLM_GGUF_REPO:-bartowski/Llama-3.2-3B-Instruct-GGUF}"
LLM_URL="https://huggingface.co/${LLM_REPO}/resolve/main/${LLM_FILE}"

NOMIC_FILE="${EMBEDDING_GGUF_FILE:-nomic-embed-text-v1.5.Q4_K_M.gguf}"
NOMIC_REPO="${NOMIC_GGUF_REPO:-nomic-ai/nomic-embed-text-v1.5-GGUF}"
NOMIC_URL="https://huggingface.co/${NOMIC_REPO}/resolve/main/${NOMIC_FILE}"

echo "Required models:"
echo "  LLM        : ${LLM_FILE}  (~2.0 GB)"
echo "  Embeddings : ${NOMIC_FILE}  (~260 MB)"
echo ""

mkdir -p gguf_models

# ── Pick a downloader ────────────────────────────────────────────────
DL=""
if command -v wget  >/dev/null 2>&1; then DL="wget";  fi
if command -v curl  >/dev/null 2>&1; then DL="curl";  fi

if [[ -z "$DL" ]]; then
    echo "❌  Neither curl nor wget found.  Install one and retry."
    echo ""
    echo "Manual download URLs:"
    echo "  ${LLM_URL}"
    echo "  ${NOMIC_URL}"
    exit 1
fi

download() {
    # Download to a temporary name and atomically rename into place. A running
    # container bind-mounts this directory read-only and mmaps the GGUF files;
    # writing straight to the final name would let it map a half-written file
    # (and later fault with SIGBUS), and an interrupted download would leave a
    # partial file that looks complete. set -e aborts before the rename if the
    # transfer fails, so a broken download never lands at the real path.
    local url="$1" dest="$2" tmp="$2.part"
    if [[ "$DL" == "curl" ]]; then
        curl -L --fail --progress-bar -o "$tmp" "$url"
    else
        wget --progress=bar:force:noscroll -O "$tmp" "$url"
    fi
    mv -f "$tmp" "$dest"
}

# ── LLM model ────────────────────────────────────────────────────────
if [[ -f "gguf_models/${LLM_FILE}" ]]; then
    echo "✅  LLM model already present: ${LLM_FILE}"
else
    echo "📥  Downloading LLM: ${LLM_FILE}"
    echo "    From: ${LLM_URL}"
    echo ""
    download "${LLM_URL}" "gguf_models/${LLM_FILE}"
    echo ""
    echo "✅  Downloaded ${LLM_FILE}"
fi

# ── Embedding model ──────────────────────────────────────────────────
if [[ -f "gguf_models/${NOMIC_FILE}" ]]; then
    echo "✅  Embedding model already present: ${NOMIC_FILE}"
else
    echo "📥  Downloading embeddings: ${NOMIC_FILE}"
    echo "    From: ${NOMIC_URL}"
    echo ""
    download "${NOMIC_URL}" "gguf_models/${NOMIC_FILE}"
    echo ""
    echo "✅  Downloaded ${NOMIC_FILE}"
fi

echo ""
echo "═══════════════════════════════════════════"
echo "  All models ready in ./gguf_models/"
echo "  Run:  docker compose up --build"
echo "═══════════════════════════════════════════"
