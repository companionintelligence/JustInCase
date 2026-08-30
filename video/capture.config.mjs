/**
 * Capture stage for Just In Case.
 *
 * ── The stage ────────────────────────────────────────────────────────────────
 * JIC's UI is entirely static files. `src/server.cpp:588` does
 * `svr.set_mount_point("/", "public")` and registers exactly three JSON
 * endpoints — `GET /status`, `GET /api/library`, `POST /query`. There is no
 * framework, no bundler, no router and no auth, so the whole front end can be
 * served by any static file server:
 *
 *     python3 -m http.server 8080 --directory public
 *
 * That is what `npm run capture` points at. The three JSON endpoints are
 * stubbed below from committed fixtures.
 *
 * ── Why mocks and not the real backend ───────────────────────────────────────
 * Filming the real container is not currently possible in a repeatable way:
 *
 *   1. NON-DETERMINISTIC ANSWERS. `handle_query` (src/server.cpp) ends in
 *      `llm->generate(prompt)` — a local Llama-3.2-3B sampling loop. The answer
 *      text differs run to run, so every capture would produce a different PNG
 *      and a shot diff could never tell a
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
 *   ?stage=offline    the ready appliance with a ZIM library mounted, captured
 *                     with ALL EGRESS BLOCKED — see below
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
 *   · sidebar         style.css:744-764 hides the sidebar off-canvas below
 *                     920px, which includes the 360px mobile capture viewport.
 *                     capture.mjs runs ONE `before` list per shot across BOTH
 *                     viewports (the viewport loop is outer, the shot loop
 *                     inner), so a shot cannot simply click #sidebar-toggle —
 *                     that button is display:none at 1280px (style.css:397) and
 *                     the click would time out. Every drawer shot therefore uses
 *                     a VIEWPORT-CONDITIONAL eval:
 *
 *                       getComputedStyle(#sidebar-toggle).display !== 'none'
 *                         ? #sidebar-toggle.click()   // 360px: the real handler
 *                         : #sidebar.classList.add('open')   // 1280px: no-op
 *
 *                     The click path matters. toggleSidebar() (app.js:379-385)
 *                     also unhides #backdrop, and at 360px that dims the chat
 *                     behind the drawer (style.css:757-762). An unconditional
 *                     classList.add() skips the handler and photographs an open
 *                     drawer over an UNDIMMED chat — a state the app never
 *                     actually produces. Above 920px the eval is inert, because
 *                     `.sidebar.open` only exists inside the media query.
 *
 *                     COROLLARY: a shot whose ONLY difference from another shot
 *                     is that eval is a DUPLICATE at 1280px. `empty-library`
 *                     was exactly that — it differed from `welcome-empty` only
 *                     by the drawer, so the two desktop PNGs came out
 *                     byte-identical (sha256 af669a4b…) and the 16:9
 *                     `first-launch` scene cross-dissolved one image into
 *                     itself, as a double-exposure ghost: scenes.mjs gives f0
 *                     `push-in` (scale 1.045) and f1 `drift-down` (y -6%), so
 *                     the two identical layers drift apart mid-dissolve. It has
 *                     been removed. The storyboard has no per-format shot
 *                     selection (schema `shot` has no viewport/format key, and
 *                     scenes.mjs shotPath keys purely off fmt.name), so a frame
 *                     that only pays off in portrait cannot be bought without
 *                     charging the landscape cut for it.
 *   · library scroll  The capture `{ scroll: n }` action calls window.scrollTo,
 *                     which cannot move #library-list — that pane is its own
 *                     scroll container (`.library { flex: 1; overflow-y: auto }`,
 *                     style.css:263-268). `library-scrolled` sets .scrollTop
 *                     directly via eval; FREEZE_CSS forces `scroll-behavior:
 *                     auto`, so it lands instantly and byte-stably.
 *   · composer text   `question-typed` uses `{ fill: [...] }` and then resets
 *                     the input's caret and scrollLeft to 0. Without that reset
 *                     the 360px frame shows the question scrolled to its END
 *                     ("…l creek water to make it safe?"). FREEZE_CSS blanks the
 *                     caret, so no cursor blink drifts between runs.
 *                     The question it types must NOT be one of the six
 *                     SUGGESTED_PROMPTS (app.js:29-36) — those render as chips
 *                     directly above the composer in the same frame, and a
 *                     shot captioned "type your own" that echoes a chip
 *                     verbatim demonstrates the opposite of what it claims.
 *                     `answer` and `sources-open` type and submit the SAME
 *                     string rather than clicking a chip, so the conversation
 *                     the video shows is the one it just watched being typed.
 *                     The stubbed POST /query returns query.water.json for any
 *                     question, so the answer stays on-topic for any phrasing
 *                     of the water beat.
 *   · fonts           Abel and Source Code Pro are vendored under
 *                     public/assets/fonts — no CDN, nothing to fail offline.
 *   · chat scroll     scrollChat() pins the log to the bottom; stable for fixed
 *                     fixture content, but it will reframe if the answer text
 *                     in fixtures/query.water.json changes length.
 *   · match scores    fixtures/query.water.json carries NO `score` key, on
 *                     purpose — do not add one back. app.js:119 renders it as
 *                     `match ${(score * 100).toFixed(0)}%`, but the value the
 *                     real server sends is a raw Reciprocal-Rank-Fusion sum
 *                     (src/sqlite_vec_index.h:182,203,226 — `1/(60 + rank)`
 *                     accumulated over the vector and BM25 lists, and
 *                     server.cpp:190 is its only call site). The best a chunk
 *                     can score is 2/61 = 0.0328, so the shipped product can
 *                     never render more than "match 3%": a fixture with
 *                     "score": 0.84 filmed a number the app cannot produce.
 *                     app.js:119 guards with `if (m.score)`, so omitting the
 *                     key renders the source list with the filename and the
 *                     matched passage and no percentage. Put scores back only
 *                     once the index normalises RRF to a relative confidence,
 *                     and then take the values from a provisioned appliance.
 *                     `sources-open` therefore waits on `.source-item
 *                     .source-snippet`, not `.source-score` — the old wait
 *                     would now hang until the capture timed out.
 *
 * ── Known coverage gap ───────────────────────────────────────────────────────
 * There is no "ingestion in progress" screen to film. refreshStatus() has
 * exactly three display branches (app.js:220-232): LLM missing → err,
 * documents_indexed === 0 → warn "Index empty", otherwise ok. A half-indexed
 * appliance renders pixel-identically to a fully-indexed one apart from two
 * numbers, so a status.indexing.json fixture would buy a near-duplicate frame.
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
  offline: fixture("status.offline.json"),
};

const LIBRARY = {
  empty: fixture("library.empty.json"),
  ready: fixture("library.ready.json"),
  // Degraded means the chat model is absent, not the index: the library stays
  // browsable, exactly as the app's own banner promises.
  degraded: fixture("library.ready.json"),
  // Same 32 documents; the ZIM library is reported through /status, not here.
  offline: fixture("library.ready.json"),
};

const QUERY = {
  empty: fixture("query.water.json"),
  ready: fixture("query.water.json"),
  degraded: fixture("query.water.json"),
  // The offline beat's answer cites a field manual AND an encyclopedia
  // article — the mix the ZIM retriever exists to produce.
  offline: fixture("query.water.offline.json"),
};

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
  const m = /[?&]stage=(empty|ready|degraded|offline)/.exec(from);
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
    // ── The egress guard ─────────────────────────────────────────────────
    //
    // The film claims this appliance works with no network. That claim is
    // ENFORCED here rather than asserted in a caption: every request whose
    // origin is not the local stage is ABORTED, and any that occurs is
    // recorded and thrown at the end of the capture.
    //
    // So the frames are not a dramatisation of being offline — the page that
    // was photographed genuinely could not reach anything, and if a future
    // change ever made the UI fetch a font, an analytics beacon or a CDN
    // script, the capture FAILS instead of quietly filming a product that
    // phones home while the narration says it does not.
    //
    // This runs for every stage, not just ?stage=offline. There is no shot in
    // this film that should be contacting the internet.
    const egress = [];
    const localOrigin = new URL(process.env.APP_URL ?? "http://localhost:8080").origin;
    await context.route("**/*", (route) => {
      const url = route.request().url();
      // data: and blob: never leave the page; the stage origin is the app.
      if (url.startsWith("data:") || url.startsWith("blob:") || url.startsWith(localOrigin)) {
        return route.fallback();
      }
      const line = `${route.request().method()} ${url}`;
      if (!egress.includes(line)) {
        egress.push(line);
        // Reported the INSTANT it happens, not only at teardown. A throw from
        // a route callback or a "close" listener is swallowed by Playwright,
        // so a violation that only surfaced at the end could be lost entirely
        // — and a silently-passing honesty check is worse than none.
        console.error(`  ✗ EGRESS BLOCKED — the offline claim would be false: ${line}`);
      }
      return route.abort();
    });
    context.on("close", () => {
      if (egress.length) {
        console.error(
          `\n  ✗ ${egress.length} external request(s) were attempted during capture:\n    ` +
            egress.join("\n    ") +
            `\n    Every frame in this film claims the appliance needs no network.\n` +
            `    Fix the app or stop making the claim — do not ship these shots.\n`
        );
        // Best-effort hard failure. Playwright may swallow this; the lines
        // above are the guarantee, this is the belt.
        process.exitCode = 1;
      }
    });

    // The three JSON endpoints a static file server cannot answer.
    await context.route("**/status", (route) => json(route, STATUS[stageOf(route)]));
    await context.route("**/api/library", (route) => json(route, LIBRARY[stageOf(route)]));
    await context.route("**/query", (route) => {
      if (route.request().method() !== "POST") return route.fallback();
      return json(route, QUERY[stageOf(route)]);
    });

    // An unstyled capture looks like a UI regression but is usually a network
    // failure. JIC ships its own fonts, so anything failing here is real.
    context.on("requestfailed", (req) => {
      console.warn(`  ! request failed: ${req.method()} ${req.url()}`);
    });
  },
};
