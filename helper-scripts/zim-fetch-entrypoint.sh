#!/bin/sh
set -eu

# ═══════════════════════════════════════════════════════════════════════
#  JIC — ZIM fetcher (runs INSIDE the fetch container)
#
#  Entrypoint of the `zim-fetch` compose service. Downloads Kiwix archives
#  straight into the `jic-zim` volume that kiwix-serve reads.
#
#  ── Why this runs in a container and not on the host ──────────────────
#  A named volume's mountpoint is not writable from the host on Docker
#  Desktop (it lives inside a VM), so a host-side downloader either fails
#  with a permission error halfway through a multi-gigabyte transfer, or
#  needs a second copy step. Downloading from inside a container that has
#  the volume mounted works identically on Linux, macOS and Windows, and
#  never asks the user for sudo.
#
#  ── Why aria2 ─────────────────────────────────────────────────────────
#  One binary that speaks HTTPS, BitTorrent, magnet AND Metalink4, verifies
#  checksums, and resumes. Kiwix publishes every one of those for every ZIM
#  (verified: .torrent is application/x-bittorrent, .magnet, .sha256, and
#  the OPDS catalogue advertises .zim.meta4), so a torrent fetch here is
#  using the publisher's own infrastructure rather than a side channel.
#
#  ── THE STAGING DIRECTORY IS NOT OPTIONAL ─────────────────────────────
#  kiwix-serve is started with a `/data/*.zim` glob. If a download wrote
#  its target name directly into /data, a restart mid-download would mount
#  a TRUNCATED archive — a library that is quietly missing most of its
#  articles, with nothing on screen saying so. Everything therefore lands
#  in /data/.incoming/ (dot-prefixed, so the glob cannot see it) and is
#  moved into place only after its checksum is verified.
# ═══════════════════════════════════════════════════════════════════════

MIRROR="${ZIM_MIRROR:-https://download.kiwix.org/zim}"
DEST="${ZIM_DEST:-/data}"
STAGE="$DEST/.incoming"
# Minutes to keep seeding after a torrent completes. 0 = take and leave.
# Off by default: an appliance in a household should not start using the
# owner's upstream bandwidth because a download finished.
SEED_MINUTES="${ZIM_SEED_MINUTES:-0}"
# http | torrent — torrent additionally verifies every piece against the
# publisher's hashes as it downloads, and uses webseeds so it still runs at
# full speed with zero peers.
METHOD="${ZIM_METHOD:-torrent}"

# name|directory|filename-prefix|approx size|licence
# Resolved against download.kiwix.org on 2026-08-08. sources.yaml also lists
# a WikiHow ZIM: it is absent because it does not exist to download —
# /zim/wikihow/ 404s and the catalogue returns zero entries for it.
PACKS="
simple-wikipedia|wikipedia|wikipedia_en_simple_all_nopic|~1 GB|CC BY-SA
medicine|wikipedia|wikipedia_en_medicine_nopic|~1.7 GB|CC BY-SA
ifixit|ifixit|ifixit_en_all|~3 GB|CC BY-NC-SA
gutenberg|gutenberg|gutenberg_en_all|~65 GB|PD / mixed
wikipedia|wikipedia|wikipedia_en_all_nopic|~97 GB|CC BY-SA
"

usage() {
  echo "JIC — ZIM library fetcher"
  echo
  echo "  docker compose --profile library run --rm zim-fetch [pack...]"
  echo "  docker compose --profile library run --rm zim-fetch --list"
  echo
  printf "  %-18s %-10s %s\n" PACK SIZE LICENCE
  echo "$PACKS" | while IFS='|' read -r n d p s l; do
    [ -z "${n:-}" ] && continue
    printf "  %-18s %-10s %s\n" "$n" "$s" "$l"
  done
  echo
  echo "  Default set: ${ZIM_DEFAULT_PACKS:-simple-wikipedia medicine ifixit}"
  echo "  gutenberg and wikipedia are excluded from it — 65-97 GB is a"
  echo "  deliberate choice, not a default."
  echo
  echo "  Env:  ZIM_METHOD=torrent|http   ZIM_SEED_MINUTES=0"
}

case "${1:-}" in
  --help|-h|--list) usage; exit 0 ;;
esac

mkdir -p "$STAGE"

# Newest dated file for a pack. Kiwix publishes dated names
# (wikipedia_en_simple_all_nopic_2026-05.zim) and prunes old dates as new
# ones land, so a hard-coded URL in a repo is a 404 with a shelf life —
# the worst failure mode for a script somebody runs once, months later,
# during an emergency.
resolve() {
  curl -fsSL --max-time 60 "$MIRROR/$1/" 2>/dev/null \
    | grep -oE "$2_[0-9]{4}-[0-9]{2}\.zim" | sort -u | tail -n 1
}

REQUESTED="$*"
[ -z "$REQUESTED" ] && REQUESTED="${ZIM_DEFAULT_PACKS:-simple-wikipedia medicine ifixit}"

echo "═══════════════════════════════════════════"
echo "  JIC — ZIM library fetch"
echo "  method $METHOD   dest $DEST"
echo "═══════════════════════════════════════════"

failed=0
for want in $REQUESTED; do
  entry=$(echo "$PACKS" | grep "^$want|" || true)
  if [ -z "$entry" ]; then
    echo "✗ unknown pack: $want   (--list to see them)"; failed=1; continue
  fi
  dir=$(echo "$entry" | cut -d'|' -f2)
  prefix=$(echo "$entry" | cut -d'|' -f3)
  size=$(echo "$entry" | cut -d'|' -f4)
  lic=$(echo "$entry" | cut -d'|' -f5)

  echo
  echo "── $want  ($size, $lic)"

  file=$(resolve "$dir" "$prefix")
  if [ -z "$file" ]; then
    echo "   ✗ could not resolve a current file for $prefix under $MIRROR/$dir/"
    failed=1; continue
  fi

  if [ -f "$DEST/$file" ]; then
    echo "   ✓ already present: $file"
    continue
  fi

  echo "   ↓ $file"
  if [ "$METHOD" = "torrent" ]; then
    # --check-integrity ONLY when there is a partial file to re-verify.
    # Passing it on a fresh download makes aria2 hash a file that does not
    # exist yet and log "Checksum error detected" before downloading
    # anything — an ERROR line on a completely healthy run, which is how
    # people learn to ignore error lines. BitTorrent verifies every piece
    # against the torrent's hashes as it downloads regardless, so nothing
    # is lost by leaving it off for a fresh fetch.
    RESUME=""
    [ -f "$STAGE/$file" ] && RESUME="--check-integrity=true"
    # DHT off by default. It is not needed — the torrent carries an HTTPS
    # tracker (tracker.openzim.org) for peer discovery AND four webseeds —
    # and leaving it on makes an appliance chatter UDP at the open
    # internet, plus logs a scary "Failed to load DHT routing table" on
    # every first run because there is no table yet. ZIM_ENABLE_DHT=1 to
    # opt in.
    DHT="--enable-dht=false"
    [ "${ZIM_ENABLE_DHT:-0}" = "1" ] && DHT="--enable-dht=true"
    # aria2 cannot speak udp:// trackers in this build ("udp is not
    # supported yet"), so they are excluded rather than attempted. Nothing
    # is lost: the torrent also carries https://tracker.openzim.org/announce
    # for peer discovery, plus four webseeds.
    #
    # The exclusion is not airtight — measured over repeated runs it
    # suppresses the message MOST of the time but not always, because aria2
    # can fire one announce before the filter applies. So an occasional
    # "udp is not supported yet" on an otherwise perfect run is expected and
    # benign; it is not evidence the download failed. Said plainly here
    # rather than claimed away, because the alternative is a comment that
    # promises silence and a reader who stops trusting the comments.
    aria2c --dir="$STAGE" \
           $RESUME $DHT \
           --bt-exclude-tracker="udp://*" \
           --continue=true \
           --seed-time="$SEED_MINUTES" \
           --summary-interval=30 \
           --console-log-level=warn \
           --bt-remove-unselected-file=true \
           "$MIRROR/$dir/$file.torrent" || { echo "   ✗ download failed"; failed=1; continue; }
    rm -f "$STAGE/$file.torrent"
  else
    aria2c --dir="$STAGE" \
           --continue=true \
           --max-connection-per-server=4 \
           --summary-interval=30 \
           --console-log-level=warn \
           "$MIRROR/$dir/$file" || { echo "   ✗ download failed"; failed=1; continue; }
  fi

  # Verify against the PUBLISHER's checksum, not only the torrent's own
  # piece hashes. A torrent can be internally consistent and still be a
  # stale edition; this is the check that says "this is the file Kiwix
  # published under this name".
  if curl -fsSL --max-time 60 -o "$STAGE/$file.sha256" "$MIRROR/$dir/$file.sha256" 2>/dev/null; then
    if (cd "$STAGE" && sha256sum -c "$file.sha256" >/dev/null 2>&1); then
      echo "   ✓ sha256 verified"
    else
      echo "   ✗ SHA-256 MISMATCH — refusing to publish $file into the library"
      rm -f "$STAGE/$file" "$STAGE/$file.sha256"
      failed=1; continue
    fi
    rm -f "$STAGE/$file.sha256"
  else
    echo "   ! no published .sha256 — torrent piece hashes are the only check"
  fi

  # Atomic within the same filesystem: kiwix-serve either sees no file or a
  # complete, verified one. Never a half-written archive.
  mv "$STAGE/$file" "$DEST/$file"
  echo "   ✓ $file"

  # Any older edition of the same pack is now superseded. kiwix-serve would
  # otherwise mount both and answer twice from the same corpus.
  for old in "$DEST/${prefix}_"*.zim; do
    [ -e "$old" ] || continue
    [ "$(basename "$old")" = "$file" ] && continue
    echo "   · superseded: $(basename "$old") — delete it when you are happy"
  done
done

echo
echo "Library now contains:"
ls -1sh "$DEST"/*.zim 2>/dev/null || echo "  (nothing yet)"
echo
echo "Next:  docker compose --profile library up -d   →  http://localhost:8081"
exit $failed
