#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════
# JIC Torrent Creator — wrapper for create-torrent.py
# ══════════════════════════════════════════════════════════════════════
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON="${PYTHON:-python3}"

exec "$PYTHON" "$SCRIPT_DIR/create-torrent.py" "$@"
