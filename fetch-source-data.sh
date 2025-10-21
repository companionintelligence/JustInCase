#!/usr/bin/env bash
# fetch-freebooks.sh
# Simple helper to fetch public-domain resources from popular free sources.
# USAGE examples:
#   ./fetch-freebooks.sh gutenberg 1342        # download Pride & Prejudice (id 1342) plain text/epub links
#   ./fetch-freebooks.sh archive some-archive-id
#   ./fetch-freebooks.sh standardebooks 2024-08-collection
#   ./fetch-freebooks.sh librivox 12345
#   ./fetch-freebooks.sh torrent /path/to/file.torrent
#
# REQUIREMENTS: curl or wget, aria2c preferred, and optionally "ia" (Internet Archive CLI) for archive.org convenience.

set -euo pipefail
IFS=$'\n\t'

# helpers
HAS() { command -v "$1" >/dev/null 2>&1; }
DL_DIR="${PWD}/downloads"

mkdir -p "$DL_DIR"

# use aria2c if present for parallel downloads; fallback to curl/wget single-threaded
download_file() {
  url="$1"
  dest="$2"
  mkdir -p "$(dirname "$dest")"
  if HAS aria2c; then
    aria2c -x 4 -s 4 -k 1M -o "$(basename "$dest")" -d "$(dirname "$dest")" "$url"
  elif HAS curl; then
    curl -L -o "$dest" "$url"
  elif HAS wget; then
    wget -O "$dest" "$url"
  else
    echo "Error: no downloader (aria2c / curl / wget) found." >&2
    exit 1
  fi
}

# 1) Project Gutenberg
# id = numeric ebook id. This will try common file patterns:
#  - plain text utf-8: https://www.gutenberg.org/ebooks/<id>.txt.utf-8
#  - cache path: https://www.gutenberg.org/cache/epub/<id>/pg<id>.txt
fetch_gutenberg() {
  id="$1"
  outdir="$DL_DIR/gutenberg/$id"
  mkdir -p "$outdir"
  echo "Fetching Project Gutenberg id=$id -> $outdir"

  urls=(
    "https://www.gutenberg.org/ebooks/${id}.txt.utf-8"
    "https://www.gutenberg.org/cache/epub/${id}/pg${id}.txt"
    "https://www.gutenberg.org/cache/epub/${id}/pg${id}.txt.utf-8"
    "https://www.gutenberg.org/ebooks/${id}.epub.images"
    "https://www.gutenberg.org/ebooks/${id}.epub.noimages"
    "https://www.gutenberg.org/files/${id}/${id}-0.txt"
    "https://www.gutenberg.org/files/${id}/${id}.txt"
  )

  success=0
  for u in "${urls[@]}"; do
    echo "  trying: $u"
    # test availability
    if HAS curl; then
      code=$(curl -s -o /dev/null -w "%{http_code}" "$u" || echo 000)
      if [ "$code" = "200" ]; then
        filename=$(basename "$u")
        download_file "$u" "$outdir/$filename"
        success=1
        break
      fi
    else
      if wget --spider -q "$u"; then
        filename=$(basename "$u")
        download_file "$u" "$outdir/$filename"
        success=1
        break
      fi
    fi
  done

  if [ "$success" -eq 0 ]; then
    echo "Could not find an obvious direct file for Gutenberg id=${id}. Opening landing page instead."
    echo "Landing page: https://www.gutenberg.org/ebooks/${id}"
    echo "You can inspect that page for available formats, or pass a direct file URL to the script."
  else
    echo "Saved into $outdir"
  fi
}

# 2) Internet Archive (archive.org)
# item = archive identifier (e.g., 'pride-and-prejudice_2006_librivox' or 'exampleitem')
fetch_archive() {
  item="$1"
  outdir="$DL_DIR/archive/$item"
  mkdir -p "$outdir"
  echo "Fetching Internet Archive item=$item -> $outdir"

  if HAS ia; then
    # use ia CLI for convenience (it will download primary files)
    echo "Using 'ia' CLI to download item (ia download $item)"
    ia download "$item" --dest="$outdir" || {
      echo "ia CLI failed, falling back to wget/curl downloads"
    }
  fi

  # Best-effort: try to download common zip or torrent endpoints if present
  base="https://archive.org/download/${item}"
  # try RSS / listing - we will attempt some common file endings
  candidates=(
    "${base}/${item}_archive.zip"
    "${base}/${item}.zip"
    "${base}/${item}.tar"
    "${base}/${item}.pdf"
  )
  found=0
  for c in "${candidates[@]}"; do
    echo "  trying $c"
    if HAS curl; then
      code=$(curl -s -o /dev/null -w "%{http_code}" "$c" || echo 000)
      if [ "$code" = "200" ]; then
        download_file "$c" "$outdir/$(basename "$c")"
        found=1
        break
      fi
    else
      if wget --spider -q "$c"; then
        download_file "$c" "$outdir/$(basename "$c")"
        found=1
        break
      fi
    fi
  done

  if [ "$found" -eq 0 ]; then
    echo "No obvious archive file found at $base — try visiting: https://archive.org/details/$item"
    echo "If you know the file path under that item, pass a full URL to the script or install 'ia' CLI."
  else
    echo "Saved into $outdir"
  fi
}

# 3) LibriVox (audiobooks)
# supports direct zip downloads from librivox or via archive.org mirror.
fetch_librivox() {
  id="$1" # could be librivox book id or archive.org item
  outdir="$DL_DIR/librivox/$id"
  mkdir -p "$outdir"
  echo "Fetching LibriVox id=$id -> $outdir"
  # Try librivox zip (many books have a 'Whole book (zip)' link):
  librivox_zip="https://librivox.org/${id}/download?format=zip"  # not always predictable
  # Safer to try archive.org item with same slug
  arch="https://archive.org/download/${id}/${id}_64kb.mp3.zip"
  echo "Trying common archive.org paths..."
  if HAS curl; then
    code=$(curl -s -o /dev/null -w "%{http_code}" "$arch" || echo 000)
    if [ "$code" = "200" ]; then
      download_file "$arch" "$outdir/$(basename "$arch")"
      echo "Saved into $outdir"
      return
    fi
  else
    if wget --spider -q "$arch"; then
      download_file "$arch" "$outdir/$(basename "$arch")"
      echo "Saved into $outdir"
      return
    fi
  fi

  echo "Could not auto-find a zip for $id — visit https://librivox.org/ and search, or check https://archive.org for a mirror."
  echo "If you have a torrent for a LibriVox book (public domain), you can use the 'torrent' mode below."
}

# 4) Standard Ebooks
# They offer monthly bulk zips and per-book downloads. Example bulk: https://standardebooks.org/bulk-downloads
fetch_standardebooks() {
  slug="$1" # could be 'bulk' or a specific file slug
  outdir="$DL_DIR/standardebooks"
  mkdir -p "$outdir"
  echo "Fetching Standard Ebooks collection / slug=$slug -> $outdir"
  if [ "$slug" = "bulk" ]; then
    # try to fetch the bulk collection index (they publish zips)
    # we attempt to download the monthly collections page programmatically is fragile;
    echo "Please visit https://standardebooks.org/bulk-downloads to pick a bulk zip. This script can fetch a direct URL if you pass it."
    echo "Example: ./fetch-freebooks.sh url https://standardebooks.org/ebooks/collection-2024-08.zip"
    return
  fi
  echo "If you know the direct .zip/.epub url on standardebooks.org, use the 'url' verb to fetch it."
}

# 5) Generic URL fetch
fetch_url() {
  url="$1"
  outdir="$DL_DIR/other"
  mkdir -p "$outdir"
  fname="$(basename "$url" | sed 's/[^a-zA-Z0-9._-]/_/g')"
  echo "Downloading $url -> $outdir/$fname"
  download_file "$url" "$outdir/$fname"
}

# 6) Torrent / magnet download (ONLY for legal/public-domain torrents)
# accepts: path/to/file.torrent  OR magnet:?...
fetch_torrent() {
  t="$1"
  outdir="$DL_DIR/torrents"
  mkdir -p "$outdir"
  if ! HAS aria2c; then
    echo "Torrent download requires aria2c (or another bittorrent client). Install aria2c and retry." >&2
    exit 1
  fi
  echo "Starting torrent/magnet download (aria2c) -> $outdir"
  aria2c -d "$outdir" "$t"
  echo "aria2c exit status: $?"
  echo "Files saved into: $outdir"
}

# parse CLI
verb="${1:-}"
arg="${2:-}"

case "$verb" in
  gutenberg)
    if [ -z "$arg" ]; then
      echo "Usage: $0 gutenberg <id>"
      exit 1
    fi
    fetch_gutenberg "$arg"
    ;;
  archive)
    if [ -z "$arg" ]; then
      echo "Usage: $0 archive <archive-item-id>"
      exit 1
    fi
    fetch_archive "$arg"
    ;;
  librivox)
    if [ -z "$arg" ]; then
      echo "Usage: $0 librivox <slug-or-archive-id>"
      exit 1
    fi
    fetch_librivox "$arg"
    ;;
  standardebooks)
    fetch_standardebooks "${arg:-bulk}"
    ;;
  url)
    if [ -z "$arg" ]; then
      echo "Usage: $0 url <full-url>"
      exit 1
    fi
    fetch_url "$arg"
    ;;
  torrent)
    if [ -z "$arg" ]; then
      echo "Usage: $0 torrent <path-to-.torrent | magnet:?…>"
      exit 1
    fi
    fetch_torrent "$arg"
    ;;
  *)
    echo "Usage:"
    echo "  $0 gutenberg <id>        # try common Gutenberg file URLs for ebook id"
    echo "  $0 archive <item-id>    # download archive.org item (ia CLI recommended)"
    echo "  $0 librivox <slug>      # try to find librivox/IA zip"
    echo "  $0 standardebooks bulk  # hint: check https://standardebooks.org/bulk-downloads"
    echo "  $0 url <full-url>       # fetch any direct url"
    echo "  $0 torrent <file|magnet> # aria2c download (only for public domain content)"
    exit 1
    ;;
esac
