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
#     --category CAT    only fetch one category    (e.g. 200_Medical)
#     --collection ID   only fetch one collection  (e.g. austere-medicine)
#     --list            print the manifest and exit
#     --list-collections print the declared collections and exit
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
ONLY_COLLECTION=""
MODE="fetch"
FORCE=0
STRICT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --manifest) MANIFEST="$2"; shift 2 ;;
        --dest)     DEST="$2";     shift 2 ;;
        --seed)     SEED="$2";     shift 2 ;;
        --category) ONLY_CATEGORY="$2"; shift 2 ;;
        --collection) ONLY_COLLECTION="$2"; shift 2 ;;
        --list)     MODE="list";     shift ;;
        --list-collections) MODE="list-collections"; shift ;;
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
# Emits one record per entry, fields joined by the ASCII unit separator
# (\037 — unlike tabs, bash `read` does not collapse empty fields on it):
#   url \037 category \037 filename \037 sha256 \037 title
# Only the simple flat schema used by sources.yaml is supported.
US=$'\037'
parse_manifest() {
    awk '
        function val(line) {
            sub(/^[^:]*:[[:space:]]*/, "", line)   # strip "key: "
            sub(/[[:space:]]+#.*$/, "", line)      # strip trailing comment
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
            gsub(/^"|"$/, "", line)                # strip surrounding quotes
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
            return line
        }
        function emit() {
            if (url != "")
                printf "%s\037%s\037%s\037%s\037%s\037%s\n", url, cat, fn, sha, title, coll
            url = cat = fn = sha = title = coll = ""
        }
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*-[[:space:]]*url:/      { emit(); url   = val($0); next }
        /^[[:space:]]+filename:/              { fn    = val($0); next }
        /^[[:space:]]+category:/              { cat   = val($0); next }
        /^[[:space:]]+collection:/            { coll  = val($0); next }
        /^[[:space:]]+sha256:/                { sha   = val($0); next }
        /^[[:space:]]+title:/                 { title = val($0); next }
        END { emit() }
    ' "$MANIFEST"
}

# Emits one record per collection definition:  id \037 name \037 description
parse_collections() {
    awk '
        function val(line) {
            sub(/^[^:]*:[[:space:]]*/, "", line)
            sub(/[[:space:]]+#.*$/, "", line)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
            gsub(/^"|"$/, "", line)
            return line
        }
        function emit() {
            if (id != "") printf "%s\037%s\037%s\n", id, name, desc
            id = name = desc = ""
        }
        /^collections:[[:space:]]*$/          { incoll = 1; next }
        /^sources:[[:space:]]*$/              { if (incoll) { emit(); incoll = 0 } }
        incoll && /^[[:space:]]*-[[:space:]]*id:/ { emit(); id = val($0); next }
        incoll && /^[[:space:]]+name:/            { name = val($0); next }
        incoll && /^[[:space:]]+description:/     { desc = val($0); next }
        END { emit() }
    ' "$MANIFEST"
}

# ── Validate ─────────────────────────────────────────────────────────
validate_manifest() {
    local errors=0 count=0
    declare -A seen defined
    # Collections declared in the top-level `collections:` block
    while IFS="$US" read -r id name desc; do
        [[ -n "$id" ]] && defined[$id]=1
    done < <(parse_collections)
    while IFS="$US" read -r url cat fn sha title coll; do
        count=$((count + 1))
        local where="entry #$count (${fn:-$url})"
        if [[ ! "$url" =~ ^https?:// ]]; then
            echo "  ✗ $where: url must be http(s):// — got '$url'"; errors=$((errors+1))
        fi
        if [[ "$url" =~ ^http:// ]]; then
            echo "  ⚠ $where: plain http URL (no TLS)"
        fi
        if [[ -z "$fn" || "$fn" == */* ]]; then
            echo "  ✗ $where: filename missing or contains '/'"; errors=$((errors+1))
        fi
        if [[ ! "$fn" =~ \.(pdf|txt)$ ]]; then
            echo "  ✗ $where: filename must end in .pdf or .txt (ingestible types)"; errors=$((errors+1))
        fi
        if [[ ! "$cat" =~ ^[0-9]{3}_[A-Za-z]+$ ]]; then
            echo "  ✗ $where: category '$cat' must look like 100_Survival"; errors=$((errors+1))
        fi
        if [[ -z "$title" ]]; then
            echo "  ✗ $where: missing title"; errors=$((errors+1))
        fi
        if [[ -z "$coll" ]]; then
            echo "  ✗ $where: missing collection"; errors=$((errors+1))
        elif [[ -z "${defined[$coll]:-}" ]]; then
            echo "  ✗ $where: collection '$coll' not declared in the collections: block"; errors=$((errors+1))
        fi
        if [[ -n "$sha" && ! "$sha" =~ ^[0-9a-fA-F]{64}$ ]]; then
            echo "  ✗ $where: sha256 must be 64 hex chars"; errors=$((errors+1))
        fi
        local key="$cat/$fn"
        if [[ -n "${seen[$key]:-}" ]]; then
            echo "  ✗ $where: duplicate target $key"; errors=$((errors+1))
        fi
        seen[$key]=1
    done < <(parse_manifest)

    echo ""
    echo "Manifest: $count entries, $errors error(s)"
    [[ $errors -eq 0 ]]
}

# ── List ─────────────────────────────────────────────────────────────
list_manifest() {
    printf '%-22s %-18s %-48s %s\n' "COLLECTION" "CATEGORY" "FILENAME" "TITLE"
    while IFS="$US" read -r url cat fn sha title coll; do
        printf '%-22s %-18s %-48s %s\n' "$coll" "$cat" "$fn" "$title"
    done < <(parse_manifest | sort)
}

# Print the declared collections with a live item count for each.
list_collections() {
    declare -A cnt
    while IFS="$US" read -r url cat fn sha title coll; do
        [[ -n "$coll" ]] && cnt[$coll]=$(( ${cnt[$coll]:-0} + 1 ))
    done < <(parse_manifest)
    printf '%-24s %-6s %s\n' "ID" "ITEMS" "NAME"
    while IFS="$US" read -r id name desc; do
        printf '%-24s %-6s %s\n' "$id" "${cnt[$id]:-0}" "$name"
    done < <(parse_collections)
}

case "$MODE" in
    validate)         echo "Validating $MANIFEST ..."; validate_manifest; exit $? ;;
    list)             list_manifest; exit 0 ;;
    list-collections) list_collections; exit 0 ;;
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
    done < <(find "$SEED" -type f \( -iname '*.pdf' -o -iname '*.txt' \) -print0)
    echo "   $seeded file(s) seeded"
fi

echo "── Fetching manifest: $MANIFEST → $DEST"
ok=0; skipped=0; failed=0
failed_list=""

while IFS="$US" read -r url cat fn sha title coll; do
    [[ -n "$ONLY_CATEGORY"   && "$cat"  != "$ONLY_CATEGORY"   ]] && continue
    [[ -n "$ONLY_COLLECTION" && "$coll" != "$ONLY_COLLECTION" ]] && continue

    dir="$DEST/$cat"
    out="$dir/$fn"
    part="$out.part"

    if [[ -s "$out" && $FORCE -eq 0 ]]; then
        skipped=$((skipped + 1))
        continue
    fi

    mkdir -p "$dir"
    echo ""
    echo "📥  $title"
    echo "    $url"

    if ! curl -fL --retry 3 --retry-delay 2 --connect-timeout 20 --max-time 900 \
              -A "JIC-content-fetch/1.0 (+https://github.com/companionintelligence/JustInCase)" \
              -o "$part" "$url"; then
        echo "    ✗ download failed"
        rm -f "$part"
        failed=$((failed + 1)); failed_list+="    $cat/$fn — $url"$'\n'
        continue
    fi

    # Sanity checks before the file becomes visible to the ingester
    if [[ ! -s "$part" ]]; then
        echo "    ✗ empty download"
        rm -f "$part"; failed=$((failed + 1)); failed_list+="    $cat/$fn — empty"$'\n'
        continue
    fi
    if [[ "$fn" == *.pdf ]] && [[ "$(head -c 4 "$part")" != "%PDF" ]]; then
        echo "    ✗ not a PDF (server returned an HTML page?)"
        rm -f "$part"; failed=$((failed + 1)); failed_list+="    $cat/$fn — not a PDF"$'\n'
        continue
    fi
    if [[ -n "$sha" ]]; then
        actual=$(sha256sum "$part" | awk '{print $1}')
        if [[ "${actual,,}" != "${sha,,}" ]]; then
            echo "    ✗ sha256 mismatch (expected $sha, got $actual)"
            rm -f "$part"; failed=$((failed + 1)); failed_list+="    $cat/$fn — sha256 mismatch"$'\n'
            continue
        fi
    fi

    mv "$part" "$out"   # atomic: the ingester never sees partial files
    echo "    ✓ $(du -h "$out" | cut -f1) → $cat/$fn"
    ok=$((ok + 1))
done < <(parse_manifest)

echo ""
echo "══════════════════════════════════════════════"
echo "  Library fetch complete"
echo "    downloaded : $ok"
echo "    skipped    : $skipped (already present)"
echo "    failed     : $failed"
if [[ -n "$failed_list" ]]; then
    echo ""
    echo "  Failed downloads:"
    printf '%s' "$failed_list"
fi
echo "══════════════════════════════════════════════"

if [[ $STRICT -eq 1 && $failed -gt 0 ]]; then
    exit 1
fi
