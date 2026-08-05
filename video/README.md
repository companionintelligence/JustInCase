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

## Running the stages by hand

```bash
npm install
npx playwright install --with-deps chromium
npm run doctor        # verify node / ffmpeg / playwright / hyperframes / fonts

# stage: JIC's UI is static files, so a static file server is the whole app.
# Run this from the REPO ROOT in another terminal:
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

`assets/shots/*.png` and `assets/audio/*.mp3` **are** committed — they are the inputs, and they let
anyone render a cut without booting the real stack. Captures are byte-stable, so a diff in them
means the UI genuinely changed, and it lands as a reviewable image diff on whatever branch you
recaptured on.

`out/` is not committed, and the MP4s in it are never stored anywhere else either. They are
regenerated from the shots and the storyboard whenever a video is needed.

See [CI-Engineering `projects/product-video-pipeline/`](https://github.com/companionintelligence/CI-Engineering/tree/main/projects/product-video-pipeline)
for the full contract.
