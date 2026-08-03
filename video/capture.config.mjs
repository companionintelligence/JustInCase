/**
 * Capture stage for Just In Case.
 *
 * ── The stage ────────────────────────────────────────────────────────────────
 * JIC's UI is entirely static files. `src/server.cpp:543` does
 * `svr.set_mount_point("/", "public")` and registers exactly three JSON
 * endpoints — `GET /status`, `GET /api/library`, `POST /query`. There is no
 * framework, no bundler, no router and no auth, so the whole front end can be
 * served by any static file server:
 *
 *     python3 -m http.server 8080 --directory public
 *
 * That is what CI (and `npm run capture` locally) points at. The three JSON
 * endpoints are stubbed below from committed fixtures.
 *
 * ── Why mocks and not the real backend ───────────────────────────────────────
 * Filming the real container is not currently possible in a repeatable way:
 *
 *   1. NON-DETERMINISTIC ANSWERS. `handle_query` (src/server.cpp) ends in
 *      `llm->generate(prompt)` — a local Llama-3.2-3B sampling loop. The answer
 *      text differs run to run, so every capture would produce a different PNG
 *      and the shot-drift gate in .github/workflows/video.yml could never tell a
 *      real UI change from sampling noise.
 *   2. ~2.3 GB OF GGUF WEIGHTS. `src/config.h:84,89` default to
 *      Llama-3.2-3B-Instruct-Q4_K_M.gguf + nomic-embed-text-v1.5.Q4_K_M.gguf,
 *      host-provisioned into ./gguf_models. They are not in the repo.
 *   3. A LONG SOURCE BUILD. The Dockerfile compiles llama.cpp and MuPDF from
 *      source; an ingestion pass then has to populate data/jic.db from the 32
 *      documents in sources.yaml before /api/library returns anything.
 *
 * The fixtures in ./fixtures are therefore the capture stage's data plane. They
 * are derived from the repo's own contracts: the library listing is generated
 * from sources.yaml (32 files, real filenames and categories), and the status
 * payloads use the real field names and defaults from src/config.h
 * (JIC_VERSION 0.3.0, LLM_MODEL llama3.2:3b, EMBEDDING_MODEL nomic-embed-text).
 * The /query answer and its match snippets are SYNTHETIC — plausible, short and
 * written for the video, not extracted from the cited PDFs. Re-run capture
 * against a fully provisioned appliance when one exists and that beat becomes
 * a recording of the real model instead of a mock.
 *
 * ── Which fixture a shot gets ────────────────────────────────────────────────
 * Shots select a stage with a `?stage=` query on their capture `path`:
 *
 *   ?stage=empty      fresh install — models loaded, nothing indexed yet
 *   ?stage=ready      32 documents indexed, engine online   (default)
 *   ?stage=degraded   llm_loaded:false — the offline / LLM-missing branch
 *
 * Python's SimpleHTTPRequestHandler strips the query before resolving the path,
 * so every one of those URLs serves public/index.html unchanged.
 *
 * ── Determinism hazards found in this app ────────────────────────────────────
 *   · #st-uptime      public/app.js formatUptime() renders /status
 *                     uptime_seconds. Genuinely volatile against a real server —
 *                     MASKED on every shot.
 *   · status polling  `setInterval(refreshStatus, 8000)` in app.js init.
 *                     Playwright's `context.clock.install()` (video-kit installs
 *                     it because `fixedTime` is set) fakes page timers, so the
 *                     poll never fires and /status is fetched exactly once per
 *                     navigation. Do not set `fixedTime: false` without
 *                     re-checking this.
 *   · conversation id `crypto.randomUUID()` in newConversationId(). Random, but
 *                     never rendered — it only travels in the POST body, so it
 *                     cannot move a pixel. Left alone.
 *   · .typing dots    the `jic-typing` keyframe animation. Frozen by video-kit's
 *                     FREEZE_CSS, and every conversation shot waits for
 *                     `.msg.bot .msg-bubble`, so the indicator is already gone.
 *   · theme           app.js initTheme() reads localStorage 'jic-theme' and
 *                     falls back to dark. Playwright contexts start with empty
 *                     storage, so captures are always the dark theme.
 *   · sidebar         style.css hides the sidebar off-canvas below 920px, which
 *                     includes the 360px mobile capture viewport. The library
 *                     shot opens it with an `eval` rather than a click on
 *                     #sidebar-toggle, because that button is display:none at
 *                     the 1280px desktop viewport and the click would time out.
 *   · fonts           Abel and Source Code Pro are vendored under
 *                     public/assets/fonts — no CDN, nothing to fail offline.
 *   · chat scroll     scrollChat() pins the log to the bottom; stable for fixed
 *                     fixture content, but it will reframe if the answer text
 *                     in fixtures/query.water.json changes length.
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const fixture = (name) => fs.readFileSync(path.join(here, "fixtures", name), "utf8");

const STATUS = {
  empty: fixture("status.empty.json"),
  ready: fixture("status.ready.json"),
  degraded: fixture("status.degraded.json"),
};

const LIBRARY = {
  empty: fixture("library.empty.json"),
  ready: fixture("library.ready.json"),
  // Degraded means the chat model is absent, not the index: the library stays
  // browsable, exactly as the app's own banner promises.
  degraded: fixture("library.ready.json"),
};

const QUERY = fixture("query.water.json");

/**
 * Which fixture set a request belongs to, read off the requesting page's URL.
 * Falls back to the Referer header, then to "ready".
 */
const stageOf = (route) => {
  let from = "";
  try {
    from = route.request().frame()?.url() ?? "";
  } catch {
    /* no frame — service worker or a detached frame */
  }
  if (!from) from = route.request().headers().referer ?? "";
  const m = /[?&]stage=(empty|ready|degraded)/.exec(from);
  return m ? m[1] : "ready";
};

const json = (route, body) =>
  route.fulfill({
    status: 200,
    contentType: "application/json",
    headers: { "cache-control": "no-store" },
    body,
  });

export default {
  // The static mock stage. CI starts it with:
  //   python3 -m http.server 8080 --directory public
  baseURL: process.env.APP_URL ?? "http://localhost:8080",

  // JIC's own default is dark (app.js initTheme falls back to 'dark').
  colorScheme: "dark",
  locale: "en-US",
  timezoneId: "UTC",
  fixedTime: "2026-06-15T15:04:00Z",

  // No login wall: server.cpp registers no auth middleware and the UI has no
  // account concept, so there is no storageState to prepare.

  async onContext(context) {
    // The three JSON endpoints a static file server cannot answer.
    await context.route("**/status", (route) => json(route, STATUS[stageOf(route)]));
    await context.route("**/api/library", (route) => json(route, LIBRARY[stageOf(route)]));
    await context.route("**/query", (route) => {
      if (route.request().method() !== "POST") return route.fallback();
      return json(route, QUERY);
    });

    // An unstyled capture looks like a UI regression but is usually a network
    // failure. JIC ships its own fonts, so anything failing here is real.
    context.on("requestfailed", (req) => {
      console.warn(`  ! request failed: ${req.method()} ${req.url()}`);
    });
  },
};
