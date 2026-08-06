#!/usr/bin/env bash
#
# Build the product video ON THIS MACHINE. No GitHub Actions, nothing uploaded.
#
#   ./video/make.sh                 stage the app, capture, check, render
#   ./video/make.sh --no-render     capture and check only (much faster)
#   ./video/make.sh --only <id>     re-shoot a single shot by id
#   ./video/make.sh --keep-up       leave the stage running when it finishes
#
# The rendered cut lands in video/out/, which is gitignored — a video is a build
# artifact, rendered on demand. The screenshots land in video/assets/shots/ and
# ARE committed: without them a checkout cannot render at all. Re-capture and
# commit them whenever you change a screen the video covers.
#
# THIS FILE IS GENERIC — it is the same in every product repo. Everything
# repo-specific lives in video/stage.sh, which defines stage_up and stage_down.
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO_ROOT="$PWD"
VIDEO_DIR="$REPO_ROOT/video"

RENDER=1
KEEP_UP=0
ONLY=()
while [ $# -gt 0 ]; do
  case "$1" in
    --no-render) RENDER=0; shift ;;
    --keep-up)   KEEP_UP=1; shift ;;
    --only)      ONLY+=(--only "$2"); shift 2 ;;
    -h|--help)   sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

say()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m ! %s\033[0m\n' "$*" >&2; }
die()  { printf '\n\033[1;31m x %s\033[0m\n' "$*" >&2; exit 1; }

# ── ports ────────────────────────────────────────────────────────────────────
# A developer's machine is not a fresh CI runner: Docker, tunnels and other
# projects are already holding ports. "Is something answering HTTP 200 here?" is
# the wrong question — the first thing tried, :8080, was held by Docker and
# answered nothing, so the check passed and the server then died on EADDRINUSE.
# Ask whether the port is BOUND.
port_is_free() {
  ! lsof -nP -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1
}

# Return the preferred port if free, else the next free one above it. Stages
# export their own base-URL variable from this, so a busy port relocates the
# stage instead of failing the run.
pick_port() {
  local want="$1" p="$1"
  for _ in $(seq 1 50); do
    port_is_free "$p" && { echo "$p"; return 0; }
    p=$((p + 1))
  done
  die "no free port found near $want"
}

# ── prerequisites ────────────────────────────────────────────────────────────
# Checked up front, with the fix in the message. Discovering that ffmpeg is
# missing after a four-minute capture wastes four minutes.
command -v node >/dev/null || die "node not found"
command -v npm  >/dev/null || die "npm not found"
if [ "$RENDER" = 1 ]; then
  command -v ffmpeg  >/dev/null || die "ffmpeg not found — 'brew install ffmpeg', or use --no-render"
  command -v ffprobe >/dev/null || die "ffprobe not found — 'brew install ffmpeg', or use --no-render"
fi

# video-kit comes off the CI-Common checkout on disk, NOT the package registry.
# The package is private, so a registry install needs a token; running the kit's
# bin directly removes authentication from the local path entirely. Same
# resolution CI-Engineering's tools/make-videos.mjs uses, so both runners agree
# on which kit is in play.
KIT_REL="CI-Common/packages/video-kit/bin/ci-video.mjs"
KIT=""
if [ -n "${CI_WORKSPACE:-}" ]; then
  KIT="$CI_WORKSPACE/$KIT_REL"
else
  # Walk UP looking for the workspace, rather than assuming it is the parent
  # directory. This checkout may be a git worktree (../ is .worktrees/) or a
  # nested clone, and in both cases the sibling-directory guess is wrong.
  d="$REPO_ROOT"
  while [ "$d" != "/" ]; do
    if [ -f "$d/$KIT_REL" ]; then KIT="$d/$KIT_REL"; break; fi
    d="$(dirname "$d")"
  done
fi
[ -n "$KIT" ] && [ -f "$KIT" ] || die "video-kit not found (looked for */$KIT_REL above $REPO_ROOT)
     Clone CI-Common into the workspace, or set CI_WORKSPACE to the directory holding it."

# The one way to invoke the kit. stage.sh uses this too, so a multi-pass
# capture_all cannot accidentally fall back to the npm script — which would go
# looking for the private package in the registry and ask for a token.
kit() { node "$KIT" "$@"; }

# ── the stage ────────────────────────────────────────────────────────────────
# stage.sh is this repo's own staging, ported from the workflow. It must define:
#   stage_up    — start whatever the capture films, and return once it answers
#   stage_down  — stop it (must be safe to call when nothing is running)
# and may set STAGE_LOG to a file worth printing when something fails.
[ -f "$VIDEO_DIR/stage.sh" ] || die "video/stage.sh not found"
# shellcheck source=/dev/null
. "$VIDEO_DIR/stage.sh"

STAGE_LOG="${STAGE_LOG:-$(mktemp -t ci-video-stage)}"
STAGE_STARTED=0

cleanup() {
  local code=$?
  if [ "$STAGE_STARTED" = 1 ] && [ "$KEEP_UP" = 0 ]; then
    say "stopping the stage"
    stage_down || warn "stage_down reported a problem"
  elif [ "$STAGE_STARTED" = 1 ]; then
    warn "leaving the stage running (--keep-up)"
  fi
  if [ $code -ne 0 ] && [ -s "$STAGE_LOG" ]; then
    echo "--- stage log (tail) ---" >&2
    tail -40 "$STAGE_LOG" >&2
  fi
  exit $code
}
trap cleanup EXIT INT TERM

say "starting the stage"
STAGE_STARTED=1
stage_up

# ── capture and render ───────────────────────────────────────────────────────
cd "$VIDEO_DIR"

# Playwright is a PEER dependency on purpose: the kit resolves it from THIS
# project (capture.mjs loadPlaywright uses createRequire against video/), so each
# repo pins the version its own e2e suite uses. It is public, so installing it
# needs no auth — and installing it on its own avoids `npm install` reaching for
# the private kit in package.json, which is the thing that would want a token.
PW_RANGE="$(node -p "require('./package.json').devDependencies.playwright" 2>/dev/null || echo 'latest')"
if ! node -e "require.resolve('playwright')" >/dev/null 2>&1; then
  say "installing Playwright ($PW_RANGE)"
  # Installed through a scratch project, NOT `npm install playwright` in here.
  # Even when told to add one package, npm resolves the whole of
  # video/package.json — which still lists the private video-kit — so it demands
  # a registry token for a dependency this script deliberately no longer uses.
  # A throwaway manifest contains only playwright, and playwright is public.
  _pw_tmp="$(mktemp -d)"
  ( cd "$_pw_tmp" && npm init -y >/dev/null 2>&1 && npm install --silent "playwright@$PW_RANGE" ) \
    || die "could not install playwright@$PW_RANGE"
  mkdir -p node_modules
  cp -R "$_pw_tmp/node_modules/." node_modules/
  rm -rf "$_pw_tmp"
  node -e "require.resolve('playwright')" >/dev/null 2>&1 \
    || die "playwright still not resolvable from $VIDEO_DIR after install"
fi

say "installing Chromium for Playwright"
# Idempotent and cached under ~/Library/Caches/ms-playwright, so a no-op after
# the first run. No --with-deps: that is an apt path and does nothing on macOS.
#
# npx, not a path into node_modules: playwright often resolves from the REPO's
# node_modules rather than video/'s (node walks up, and the stage has usually
# just run npm ci at the repo root), so a hardcoded video/node_modules/playwright
# path is simply absent. npx walks up the same way node does. It is a public
# package, so even a registry fetch here needs no auth.
npx --yes playwright install chromium

say "capturing the UI"
# Some repos cannot capture in one pass — a shot may need a different identity,
# a different seeded state, or a stage step in between. Those define capture_all
# in stage.sh and own the whole sequence; everyone else gets the single pass.
# --only always wins, so re-shooting one shot never re-runs a multi-pass script.
if declare -f capture_all >/dev/null && [ ${#ONLY[@]} -eq 0 ]; then
  capture_all
else
  kit capture "${ONLY[@]+"${ONLY[@]}"}"
fi

say "building the compositions"
kit build

say "checking them"
kit check

if [ "$RENDER" = 1 ]; then
  say "rendering"
  kit render
  say "done"
  ls -lh out/*.mp4 2>/dev/null || warn "no mp4 found in video/out/"
else
  say "done — capture only (--no-render); screenshots in video/assets/shots/"
fi
