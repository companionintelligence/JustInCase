#!/usr/bin/env bash
#
# JustInCase's stage, for local video capture. Sourced by video/make.sh, which
# owns the generic capture/build/check/render steps; this file only has to get
# the app serving and stop it again.
#
# The stage is the static site in public/ — no build, no backend, no seed data.
# A plain HTTP server is all the capture needs.

STAGE_PID=""

stage_up() {
  # Never reuse a port somebody else holds: a capture against another server
  # succeeds and produces plausible, wrong screenshots. Relocate instead of
  # failing — capture.config.mjs reads APP_URL, so the kit follows.
  #
  # 8080 in particular is worth expecting to be busy: Docker binds it on a
  # developer machine, and it answers nothing, so "is something serving here?"
  # is not the question — "is the port bound?" is.
  STAGE_PORT="$(pick_port 8080)"
  STAGE_URL="http://localhost:$STAGE_PORT"
  export APP_URL="$STAGE_URL"
  [ "$STAGE_PORT" = 8080 ] || echo "note: :8080 was busy, using :$STAGE_PORT"

  command -v python3 >/dev/null || { echo "python3 not found — needed to serve public/" >&2; return 1; }

  ( cd "$REPO_ROOT" && python3 -m http.server "$STAGE_PORT" --directory public ) \
    > "$STAGE_LOG" 2>&1 &
  STAGE_PID=$!

  for _ in $(seq 1 30); do
    curl -fsS -o /dev/null "$STAGE_URL/" 2>/dev/null && break
    kill -0 "$STAGE_PID" 2>/dev/null || { echo "http server exited during startup" >&2; return 1; }
    sleep 1
  done
  curl -fsS -o /dev/null "$STAGE_URL/" || { echo "http server never answered on :$STAGE_PORT" >&2; return 1; }
  echo "stage up on $STAGE_URL (static public/)"
}

stage_down() {
  [ -n "$STAGE_PID" ] || return 0
  kill -0 "$STAGE_PID" 2>/dev/null || return 0
  # Children first, then the parent. Not the process group: a background job in
  # a non-interactive shell shares the script's own group, so signalling the
  # group would kill make.sh too.
  pkill -P "$STAGE_PID" 2>/dev/null || true
  kill "$STAGE_PID" 2>/dev/null || true
  wait "$STAGE_PID" 2>/dev/null || true
}
