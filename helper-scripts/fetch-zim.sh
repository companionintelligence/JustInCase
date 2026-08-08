#!/bin/bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════════
#  JIC — ZIM library download
#
#  Fills the `jic-zim` volume that the `library` compose profile serves,
#  so that answers can be grounded in — and cite — Wikipedia and the other
#  Kiwix archives. Closes the "Kiwix ZIM ingestion" item in architecture.md
#  §12; the shortlist below is the one already written down in sources.yaml.
#
#  Usage:
#     ./helper-scripts/fetch-zim.sh                # the default shortlist
#     ./helper-scripts/fetch-zim.sh medicine       # one pack
#     ./helper-scripts/fetch-zim.sh --list         # what is available
#     ZIM_DEST=/some/dir ./helper-scripts/fetch-zim.sh
#
#  WHY THE URL IS RESOLVED AND NOT HARD-CODED.
#  Kiwix publishes dated filenames — wikipedia_en_simple_all_nopic_2026-05.zim,
#  not wikipedia_en_simple_all_nopic.zim — and prunes old dates as new ones
#  land. A hard-coded URL in a repo is therefore a 404 with a shelf life,
#  which is the worst failure mode for a download script that people run once,
#  months later, in an emergency-prep context. So each pack names a stable
#  DIRECTORY plus a filename PREFIX, and the newest matching file is resolved
#  from the directory index at run time.
# ═══════════════════════════════════════════════════════════════════════

MIRROR="${ZIM_MIRROR:-https://download.kiwix.org/zim}"

# name|directory|filename-prefix|approx size|licence
#
# Every entry below was RESOLVED against download.kiwix.org on 2026-08-08, not
# copied from a wishlist. sources.yaml also lists a WikiHow ZIM (~10 GB): it is
# deliberately absent here because it does not exist to download — /zim/wikihow/
# 404s and the library catalog returns zero entries for it. Left out rather than
# shipped as a pack that fails the moment somebody picks it.
PACKS=(
  "simple-wikipedia|wikipedia|wikipedia_en_simple_all_nopic|~250 MB|CC BY-SA"
  "medicine|wikipedia|wikipedia_en_medicine_nopic|~1.7 GB|CC BY-SA"
  "ifixit|ifixit|ifixit_en_all|~3 GB|CC BY-NC-SA"
  "gutenberg|gutenberg|gutenberg_en_all|~65 GB|PD / mixed"
  "wikipedia|wikipedia|wikipedia_en_all_nopic|~97 GB|CC BY-SA"
)

# Everything except the two that would surprise someone on a laptop.
DEFAULT_PACKS="simple-wikipedia medicine ifixit"

usage() {
  echo "JIC — ZIM library download"
  echo
  echo "  ./helper-scripts/fetch-zim.sh [pack...]   (default: $DEFAULT_PACKS)"
  echo "  ./helper-scripts/fetch-zim.sh --list"
  echo
  printf "  %-18s %-10s %s\n" PACK SIZE LICENCE
  for p in "${PACKS[@]}"; do
    IFS='|' read -r name _dir _prefix size lic <<<"$p"
    printf "  %-18s %-10s %s\n" "$name" "$size" "$lic"
  done
  echo
  echo "  gutenberg and wikipedia are NOT in the default set —"
  echo "  they are 65-97 GB and should be a deliberate choice."
}

[ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ] && { usage; exit 0; }
[ "${1:-}" = "--list" ] && { usage; exit 0; }

# ── Downloader ───────────────────────────────────────────────────────
if command -v curl >/dev/null 2>&1; then DL=curl
elif command -v wget >/dev/null 2>&1; then DL=wget
else echo "Need curl or wget." >&2; exit 1
fi

fetch_stdout() {  # url → stdout
  if [ "$DL" = curl ]; then curl -fsSL --max-time 60 "$1"
  else wget -qO- --timeout=60 "$1"; fi
}

# ── Destination ──────────────────────────────────────────────────────
# By default write straight into the compose volume so `--profile library`
# just works. Falls back to a local directory when Docker is not around,
# which is also what CI does.
DEST="${ZIM_DEST:-}"
VOLUME="${ZIM_VOLUME:-justincase_jic-zim}"
if [ -z "$DEST" ]; then
  if command -v docker >/dev/null 2>&1 && docker volume inspect "$VOLUME" >/dev/null 2>&1; then
    DEST="$(docker volume inspect "$VOLUME" --format '{{ .Mountpoint }}')"
    echo "Destination: docker volume $VOLUME"
    # On Docker Desktop the mountpoint is inside a VM and is not writable from
    # the host. Say so rather than failing later with a confusing permission
    # error halfway through a multi-gigabyte download.
    if [ ! -w "$DEST" ]; then
      echo
      echo "  The volume's mountpoint is not writable from this host"
      echo "  (normal on Docker Desktop, where it lives inside a VM)."
      echo "  Download locally and copy it in instead:"
      echo
      echo "    ZIM_DEST=./zim ./helper-scripts/fetch-zim.sh"
      echo "    docker compose --profile library up -d"
      echo "    docker cp ./zim/. jic-kiwix:/data/"
      echo
      exit 1
    fi
  else
    DEST="./zim"
    echo "Destination: $DEST  (volume $VOLUME not found — start compose once to create it)"
  fi
fi
mkdir -p "$DEST"

# ── Resolve + download ───────────────────────────────────────────────
resolve() {  # dir prefix → newest filename, or "" if none
  local dir="$1" prefix="$2"
  fetch_stdout "$MIRROR/$dir/" 2>/dev/null \
    | grep -oE "${prefix}_[0-9]{4}-[0-9]{2}\.zim" \
    | sort -u | tail -n 1
}

REQUESTED=("$@")
[ ${#REQUESTED[@]} -eq 0 ] && read -r -a REQUESTED <<<"$DEFAULT_PACKS"

echo "═══════════════════════════════════════════"
echo "  JIC — ZIM library download"
echo "═══════════════════════════════════════════"
echo

failed=0
for want in "${REQUESTED[@]}"; do
  entry=""
  for p in "${PACKS[@]}"; do
    IFS='|' read -r name _d _p _s _l <<<"$p"
    [ "$name" = "$want" ] && entry="$p" && break
  done
  if [ -z "$entry" ]; then
    echo "✗ unknown pack: $want   (--list to see them)"; failed=1; continue
  fi

  IFS='|' read -r name dir prefix size lic <<<"$entry"
  echo "── $name  ($size, $lic)"

  file="$(resolve "$dir" "$prefix")"
  if [ -z "$file" ]; then
    echo "   ✗ could not resolve a current file for $prefix under $MIRROR/$dir/"
    echo "     Browse $MIRROR/$dir/ and download by hand into $DEST"
    failed=1; continue
  fi

  target="$DEST/$file"
  if [ -f "$target" ]; then echo "   ✓ already present: $file"; continue; fi

  # Any older edition of the same pack is now superseded; kiwix-serve would
  # otherwise mount both and answer twice from the same corpus.
  old="$(ls -1 "$DEST/${prefix}_"*.zim 2>/dev/null | grep -v "/$file\$" || true)"
  if [ -n "$old" ]; then
    echo "   note: superseding $(basename "$old") — delete it once this succeeds"
  fi

  echo "   ↓ $file"
  # Download to .part and rename only on success, so an interrupted run never
  # leaves a truncated .zim that kiwix-serve would try to mount.
  if [ "$DL" = curl ]; then curl -fL --progress-bar -o "$target.part" "$MIRROR/$dir/$file"
  else wget -q --show-progress -O "$target.part" "$MIRROR/$dir/$file"; fi
  mv "$target.part" "$target"
  echo "   ✓ $file"
done

echo
echo "Content in: $DEST"
ls -1sh "$DEST"/*.zim 2>/dev/null || echo "  (nothing yet)"
echo
echo "Next:"
echo "  echo 'JIC_KIWIX_URL=http://kiwix:8080' >> .env"
echo "  docker compose --profile library up -d"
echo "  → library browsable at http://localhost:8081, and cited in answers at :8080"
exit $failed
