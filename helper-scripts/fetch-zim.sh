#!/bin/bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════════
#  JIC — ZIM library download (host-side convenience wrapper)
#
#  The real work happens inside the `zim-fetch` container, which has the
#  jic-zim volume mounted. This script only saves you typing:
#
#     ./helper-scripts/fetch-zim.sh                 # the default set
#     ./helper-scripts/fetch-zim.sh medicine ifixit # named packs
#     ./helper-scripts/fetch-zim.sh --list          # what is available
#
#  ── Why it does not download anything itself ──────────────────────────
#  An earlier version did, and it could not work on Docker Desktop: a
#  named volume's mountpoint lives inside the VM and is not writable from
#  the host, so the download failed — or worse, succeeded into a directory
#  the library never reads. Doing it in the container is the same command
#  on Linux, macOS and Windows, and needs no sudo.
#
#  Everything else — pack list, BitTorrent, checksum verification, the
#  staging directory — is in helper-scripts/zim-fetch-entrypoint.sh.
# ═══════════════════════════════════════════════════════════════════════

cd "$(dirname "$0")/.."

if ! command -v docker >/dev/null 2>&1; then
  echo "docker not found — this wrapper runs the fetch inside a container." >&2
  exit 1
fi

# `docker compose` (v2) with a fallback to the legacy binary.
if docker compose version >/dev/null 2>&1; then
  DC="docker compose"
elif command -v docker-compose >/dev/null 2>&1; then
  DC="docker-compose"
else
  echo "docker compose not available." >&2
  exit 1
fi

echo "→ $DC --profile library run --rm zim-fetch $*"
echo
exec $DC --profile library run --rm zim-fetch "$@"
