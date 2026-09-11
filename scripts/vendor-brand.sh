#!/usr/bin/env bash
# vendor-brand.sh — pull the pinned Companion Intelligence brand bundle into public/assets/brand/.
#
# JustInCase is a C++/Docker app with a static public/ — no npm — so it consumes the canon
# as the brand bundle CI-Common attaches to every @companionintelligence/tokens release
# (see CI-Common/CONSUMING.md, Route B). Only what the page uses is vendored: tokens.css /
# tokens.json and the Manrope + JetBrains Mono woff2 (with their OFL licences).
#
#   scripts/vendor-brand.sh [version]     # default: the version in public/assets/brand/.version
#   scripts/vendor-brand.sh --check       # verify the vendored files against SHA256SUMS (CI)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/../public/assets/brand"
if [ "${1:-}" = "--check" ]; then
  ( cd "$OUT" && shasum -a 256 -c SHA256SUMS --quiet ) && echo "✔ brand assets match ci-brand $(cat "$OUT/.version")"
  exit $?
fi
VERSION="${1:-$(cat "$OUT/.version")}"
TAG="@companionintelligence/tokens@${VERSION}"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
gh release download "$TAG" --repo companionintelligence/CI-Common --pattern "ci-brand-${VERSION}.tar.gz*" --dir "$tmp"
( cd "$tmp" && shasum -a 256 -c "ci-brand-${VERSION}.tar.gz.sha256" --quiet )
tar -xzf "$tmp/ci-brand-${VERSION}.tar.gz" -C "$tmp"
B="$tmp/ci-brand-${VERSION}"
mkdir -p "$OUT/tokens" "$OUT/fonts"
cp "$B/tokens/tokens.css" "$B/tokens/tokens.json" "$OUT/tokens/"
cp "$B/fonts/manrope-latin-var.woff2" "$B/fonts/jetbrains-mono-latin-var.woff2" "$B/fonts/LICENSE-Manrope.txt" "$B/fonts/LICENSE-JetBrains-Mono.txt" "$OUT/fonts/"
grep -E 'tokens/tokens\.(css|json)|fonts/(manrope-latin-var|jetbrains-mono-latin-var)\.woff2|LICENSE-(Manrope|JetBrains-Mono)' "$B/SHA256SUMS" > "$OUT/SHA256SUMS"
echo "$VERSION" > "$OUT/.version"
( cd "$OUT" && shasum -a 256 -c SHA256SUMS --quiet )
echo "vendored ci-brand ${VERSION} into public/assets/brand"
