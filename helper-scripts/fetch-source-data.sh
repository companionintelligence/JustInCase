#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════
# JIC content fetcher — downloads the curated manifest (sources.yaml)
# into the content library.
#
# Runs in two places:
#   • on the host:        ./helper-scripts/fetch-source-data.sh
#                         (downloads into ./public/sources)
#   • inside the image:   /app/bin/fetch-sources --dest /app/public/sources
#                         (the docker compose "fetch" profile, writing
#                          into the jic-sources volume)
#
# Usage:
#   fetch-source-data.sh [options]
#     --manifest FILE   manifest to read           (default: ./sources.yaml)
#     --dest DIR        library directory          (default: ./public/sources)
#     --seed DIR        copy DIR/* into the library first (no clobber)
#     --profile PROF    only fetch profile: core | emergency-zims | full-zims | kalite | all
#                                                  (default: core)
#     --category CAT    only fetch one category    (e.g. 200_Medical, Zims_Medical)
#     --list            print the manifest and exit
#     --validate        lint the manifest (no network) and exit
#     --force           re-download files that already exist
#     --strict          exit non-zero if any download failed
#
# Downloads are atomic (written to *.part, then renamed) so the JIC
# ingestion worker never sees a half-written document.
# ══════════════════════════════════════════════════════════════════════

set -euo pipefail

MANIFEST="sources.yaml"
DEST="public/sources"
SEED=""
ONLY_CATEGORY=""
ONLY_PROFILE="core"
MODE="fetch"
FORCE=0
STRICT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --manifest) MANIFEST="$2"; shift 2 ;;
        --dest)     DEST="$2";     shift 2 ;;
        --seed)     SEED="$2";     shift 2 ;;
        --profile)  ONLY_PROFILE="$2"; shift 2 ;;
        --category) ONLY_CATEGORY="$2"; shift 2 ;;
        --list)     MODE="list";     shift ;;
        --validate) MODE="validate"; shift ;;
        --force)    FORCE=1;  shift ;;
        --strict)   STRICT=1; shift ;;
        -h|--help)  grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1 (try --help)" >&2; exit 2 ;;
    esac
done

if [[ ! -f "$MANIFEST" ]]; then
    echo "❌  Manifest not found: $MANIFEST" >&2
    exit 1
fi

# ── Parse the manifest ───────────────────────────────────────────────
# Emits one record per entry, fields joined by ASCII unit separator:
#   url \037 category \037 filename \037 sha256 \037 title \037 profile
US=$'\037'

parse_manifest() {
    awk '
        function val(line) {
            sub(/^[^:]*:[[:space:]]*/, "", line)
            sub(/[[:space:]]+#.*$/, "", line)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
            gsub(/^"|"$/, "", line)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
            return line
        }
        function emit() {
            if (url != "") {
                if (prof == "") prof = "core"
                printf "%s\037%s\037%s\037%s\037%s\037%s\n", url, cat, fn, sha, title, prof
            }
            url = cat = fn = sha = title = prof = ""
        }
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*-[[:space:]]*url:/      { emit(); url   = val($0); next }
        /^[[:space:]]+filename:/              { fn    = val($0); next }
        /^[[:space:]]+category:/              { cat   = val($0); next }
        /^[[:space:]]+profile:/               { prof  = val($0); next }
        /^[[:space:]]+sha256:/                { sha   = val($0); next }
        /^[[:space:]]+title:/                 { title = val($0); next }
        END { emit() }
    ' "$MANIFEST"
}

# ── Validate ─────────────────────────────────────────────────────────
validate_manifest() {
    local errors=0 count=0
    local nl=$'\n'
    local seen="$nl"
    while IFS="$US" read -r url cat fn sha title prof; do
        count=$((count + 1))
        local where="entry #$count (${fn:-$url})"
        if [[ ! "$url" =~ ^https?:// && ! "$url" =~ ^magnet:\? ]]; then
            echo "  ✗ $where: url must be http(s):// or magnet:? — got '$url'"; errors=$((errors+1))
        fi
        if [[ "$url" =~ ^http:// ]]; then
            echo "  ⚠ $where: plain http URL (no TLS)"
        fi
        if [[ -z "$fn" || "$fn" == */* ]]; then
            echo "  ✗ $where: filename missing or contains '/'"; errors=$((errors+1))
        fi
        if [[ ! "$fn" =~ \.(pdf|txt|zim|torrent)$ ]]; then
            echo "  ✗ $where: filename must end in .pdf, .txt, .zim, or .torrent"; errors=$((errors+1))
        fi
        if [[ ! "$cat" =~ ^[0-9A-Za-z_]+$ ]]; then
            echo "  ✗ $where: category '$cat' invalid format"; errors=$((errors+1))
        fi
        if [[ ! "$prof" =~ ^(core|emergency-zims|full-zims|kalite)$ ]]; then
            echo "  ✗ $where: profile '$prof' must be core, emergency-zims, full-zims, or kalite"; errors=$((errors+1))
        fi
        if [[ -z "$title" ]]; then
            echo "  ✗ $where: missing title"; errors=$((errors+1))
        fi
        if [[ -n "$sha" && ! "$sha" =~ ^[0-9a-fA-F]{64}$ ]]; then
            echo "  ✗ $where: sha256 must be 64 hex chars"; errors=$((errors+1))
        fi
        local key="$cat/$fn"
        if [[ "$seen" == *"$nl$key$nl"* ]]; then
            echo "  ✗ $where: duplicate target $key"; errors=$((errors+1))
        fi
        seen="$seen$key$nl"
    done < <(parse_manifest)

    echo ""
    echo "Manifest: $count entries, $errors error(s)"
    [[ $errors -eq 0 ]]
}

# ── List ─────────────────────────────────────────────────────────────
list_manifest() {
    printf '%-15s %-18s %-45s %s\n' "PROFILE" "CATEGORY" "FILENAME" "TITLE"
    while IFS="$US" read -r url cat fn sha title prof; do
        printf '%-15s %-18s %-45s %s\n' "$prof" "$cat" "$fn" "$title"
    done < <(parse_manifest | sort)
}

case "$MODE" in
    validate) echo "Validating $MANIFEST ..."; validate_manifest; exit $? ;;
    list)     list_manifest; exit 0 ;;
esac

# ── Fetch ────────────────────────────────────────────────────────────
command -v curl >/dev/null 2>&1 || { echo "❌  curl is required" >&2; exit 1; }

mkdir -p "$DEST"

# Seed: copy starter documents into the library without clobbering
if [[ -n "$SEED" && -d "$SEED" ]]; then
    echo "── Seeding library from $SEED"
    seeded=0
    while IFS= read -r -d '' f; do
        rel="${f#"$SEED"/}"
        tgt="$DEST/$rel"
        if [[ ! -f "$tgt" ]]; then
            mkdir -p "$(dirname "$tgt")"
            cp "$f" "$tgt" && seeded=$((seeded + 1))
        fi
    done < <(find "$SEED" -type f \( -iname '*.pdf' -o -iname '*.txt' -o -iname '*.zim' \) -print0)
    echo "   $seeded file(s) seeded"
fi

echo "── Fetching manifest ($ONLY_PROFILE profile): $MANIFEST → $DEST"
ok=0; skipped=0; failed=0
failed_list=""

while IFS="$US" read -r url cat fn sha title prof; do
    [[ "$ONLY_PROFILE" != "all" && "$prof" != "$ONLY_PROFILE" ]] && continue
    [[ -n "$ONLY_CATEGORY" && "$cat" != "$ONLY_CATEGORY" ]] && continue

    dir="$DEST/$cat"
    out="$dir/$fn"
    part="$out.part"

    if [[ -f "$out" && $FORCE -eq 0 ]]; then
        echo "  ↷  $cat/$fn (exists, skipping)"
        skipped=$((skipped + 1))
        continue
    fi

    mkdir -p "$dir"
    echo "  ↓  $cat/$fn — $title"

    if curl -fL --retry 3 --retry-delay 2 --connect-timeout 15 -o "$part" "$url" 2>/dev/null; then
        if [[ -n "$sha" ]]; then
            actual_sha=$(shasum -a 256 "$part" 2>/dev/null | awk '{print $1}' || sha256sum "$part" | awk '{print $1}')
            if [[ "$actual_sha" != "$sha" ]]; then
                echo "     ❌  sha256 mismatch: expected $sha, got $actual_sha"
                rm -f "$part"
                failed=$((failed + 1))
                failed_list="$failed_list\n     $cat/$fn (sha256 mismatch)"
                continue
            fi
        fi
        mv "$part" "$out"
        ok=$((ok + 1))
    else
        rm -f "$part"
        echo "     ❌  download failed: $url"
        failed=$((failed + 1))
        failed_list="$failed_list\n     $cat/$fn ($url)"
    fi
done < <(parse_manifest)

echo ""
echo "── Summary: $ok fetched, $skipped skipped, $failed failed"
if [[ $failed -gt 0 ]]; then
    echo -e "Failed downloads:$failed_list"
    [[ $STRICT -eq 1 ]] && exit 1
fi
exit 0
