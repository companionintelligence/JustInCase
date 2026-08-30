# Just In Case — product video

> **Private & Confidential — Property of Lifescope Inc. Do not distribute.**

Generates a **16:9 desktop cut** and a **9:16 mobile cut** of Just In Case's FTUE and major
screens, from this repo's own UI. Both are produced from [`storyboard.json`](storyboard.json).

## A video is build output

Cuts are rendered **locally, on demand, on a developer machine**. Nothing renders them for you:
there is no scheduled job, no CI workflow, and no stored MP4 anywhere — not as a build artifact,
not committed, not attached to a release. Render one when you need it, use it, then delete it.

The supported entry point is the local video runner in the engineering repo, which resolves the
shared video toolkit from your checkout on disk (no registry, no token). It can list every product
it knows how to build and takes a product name to build this one; see the pipeline contract linked
at the bottom of this file. The stages below are what that runner drives, and can still be run by
hand from this directory.

## Quick start

The video is built **on your own machine**. One command serves the app, captures
the UI, checks the composition, renders both cuts, and stops the server again:

```bash
./video/make.sh
```

Needs `node` and `ffmpeg` (`brew install ffmpeg`). Chromium is installed on the
first run and cached afterwards.

```bash
./video/make.sh --no-render          # capture + check only, much faster
./video/make.sh --only <shot-id>     # re-shoot a single shot
```

Where things end up:

| path | what | in git? |
|---|---|---|
| `out/*.mp4` | the rendered cuts | no — rebuild with `make.sh` |
| `assets/shots/*.png` | captured screenshots | no — inputs to the render |
| `assets/audio/*.mp3` | narration | **yes** — expensive to regenerate |

Neither the video nor the screenshots are stored: both are build artifacts. A
committed screenshot that nothing refreshes goes on looking current long after
the UI has moved, which is worse than having none.

The stage is just the static files in `public/`, so a plain HTTP server is the
whole app. `make.sh` picks a free port — 8080 when it is available — and points
the capture at it with `APP_URL`.

### Running the steps by hand

```bash
npm install
npx playwright install chromium
npm run doctor        # verify node / ffmpeg / playwright / hyperframes / fonts

# from the REPO ROOT, in another terminal:
python3 -m http.server 8080 --directory public

npm run capture       # drive the real UI -> assets/shots/*.png (both viewports)
npm run build         # storyboard.json -> build/{landscape,portrait}/index.html
npm run check         # HyperFrames gate: lint, runtime, layout, motion, contrast
npm run render        # -> out/ci-just-in-case-landscape.mp4 and out/ci-just-in-case-portrait.mp4
```

Use a different port with `APP_URL=http://localhost:18443 npm run capture` if 8080 is taken.

## The stage is mocked, on purpose

`src/server.cpp` mounts `public/` statically and adds three JSON endpoints — `GET /status`,
`GET /api/library`, `POST /query`. The static server above serves the UI; the three endpoints are
stubbed from the committed fixtures in [`fixtures/`](fixtures) by
[`capture.config.mjs`](capture.config.mjs), which selects between them with a `?stage=` query on
each shot's capture path (`empty`, `ready`, `degraded`).

The real container is not filmable today: it needs ~2.3 GB of host-provisioned GGUF weights, a
multi-stage build that compiles llama.cpp and MuPDF from source, and an ingestion pass — and its
answer text is produced by a sampling loop, so it is **non-deterministic** and every capture would
register as UI drift. `capture.config.mjs` documents each hazard in full.

## Editing the video

Everything editorial lives in [`storyboard.json`](storyboard.json) — scene order, durations,
captions, narration, and which screens appear. The **capture spec for each shot lives in the same
file**, so the script and the screenshots cannot drift apart.

Adding a beat is: add a scene, add its shot's `capture` block, `npm run capture -- --only <id>`,
then `npm run build && npm run render`.

## Media

Drop prepared media into `assets/media/` and point `media.music` at it. Per-scene voiceover is
picked up automatically from `assets/audio/<sceneId>.mp3`; regenerate it from the storyboard's
`narration` fields with `npm run narrate`.

## What is committed

`assets/audio/*.mp3` **is** committed — narration is expensive to regenerate — along with the
editorial and data planes: `storyboard.json`, `capture.config.mjs`, and `fixtures/`.

`assets/shots/*.png` **is** committed too. It used to be gitignored, on the reasoning that
capturing locally is cheap and a stale shot outlives the UI it photographed — but that trade only
holds if everyone can re-capture, and fleet-wide they cannot: only three of twelve products have a
stage that actually comes up. A gitignored shot set means nobody but the stage's author can render
the cut. So the shots are inputs, checked in like any other input, and `video/.gitignore` says so.

`build/` and `out/` are **not** committed. Both are render outputs, gitignored, and regenerated by
`./video/make.sh`. Nothing re-captures on a schedule any more, so when you move a screen the video
covers, re-run the capture yourself, watch the result before you cut, and commit the new PNGs in
the same change.

See [CI-Engineering `projects/product-video-pipeline/`](https://github.com/companionintelligence/CI-Engineering/tree/main/projects/product-video-pipeline)
for the full contract.

## Why Playwright is pinned exactly

`video/package.json` pins `playwright` to an exact version, not a range.

Captures are byte-stable — that is what makes committing the shots worthwhile, because a UI change
then lands as a reviewable image diff instead of noise. But that property only holds **within one
Chromium build**. A `^1.58.0` range resolved to 1.62.1 on one machine and rewrote every committed
shot in a repo by 0.07–0.47% of pixels: pure text antialiasing, no layout change, and completely
indistinguishable from a real UI change in review.

So the range is gone. Re-pin deliberately when you want the newer browser, and re-capture the whole
shot set in the same commit.

### The e2e suite pins separately, on purpose

`@companionintelligence/video-kit` declares `playwright` as an **optional peer dependency**
precisely so each project can pin what it needs. A difference between this directory and the e2e
suite is therefore a real decision, not a mistake to tidy away.

`tests/container/package.json` declares `@playwright/test` as `^1.48.0` — a *range*, which
resolves to 1.62.1 today. Nothing diverges right now, but that one is free to float and this one
is not.

### Why an exact pin is not sufficient on its own

Node resolution is the trap. `require.resolve("playwright")` from `video/` walks **up** the
directory tree, so a copy in the *repo's* `node_modules` satisfies it — and the repo does have
one, because the e2e suite pulls `@playwright/test` at the root. The pin here is then never
consulted: resolution succeeded, so nothing installs, and the capture runs on the root's version.
Checking that Playwright *resolves* proves nothing; only the resolved **version** does.

That check is the kit's job rather than something restated in twelve copies of this file, and the
kit does it — `video-kit/src/playwright-pin.mjs` compares the resolved version against the pin
declared here and fails the capture on drift. Set `CI_VIDEO_ALLOW_PLAYWRIGHT_DRIFT` to override,
which is never the right call for a commit that touches the shots.
