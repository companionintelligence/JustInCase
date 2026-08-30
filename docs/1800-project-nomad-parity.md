# 1800 — Project NOMAD parity

> **Private & Confidential — Property of Lifescope Inc. Do not distribute.**

Comparison of **Just In Case** against **[Project NOMAD](https://www.projectnomad.us/)**
(`Crosstalk-Solutions/project-nomad`, Apache-2.0), and what this repo has done about it.

Verified 2026-08-08 against NOMAD's site and repo, and against JIC's own source.

---

## 1. They are not the same shape

Any parity table that skips this will mislead you.

**Project NOMAD is a container orchestrator with a dashboard.** Its own README calls it "a
management UI ('Command Center') and API that orchestrates a collection of containerized tools."
It ships six management containers (AdonisJS/React admin, MySQL, Redis, an updater, a disk
collector, Dozzle) that install and configure ~16 pinned upstream images through a mounted Docker
socket — Kiwix, Ollama, Qdrant, Kolibri, and a "Supply Depot" of a dozen more. NOMAD's engineering
is in *provisioning, cataloguing and download management*. Almost none of the user-facing
capability is code NOMAD wrote.

**Just In Case is a single C++17 RAG engine.** Two static binaries from one compose file, linking
llama.cpp, MuPDF and SQLite with sqlite-vec + FTS5, over a curated ~350 MB emergency corpus. JIC's
engineering is in *retrieval quality and grounded generation*: its own inference path, its own
hybrid index, its own answer UI.

So: NOMAD is broad and shallow, JIC is narrow and deep. Most of the gaps below are JIC lacking
things NOMAD **bundled rather than built** — which is exactly why most of them are cheap to close,
and why one of them is not closeable at all.

---

## 2. Parity matrix

Severity is scored against JIC's own mission (offline emergency knowledge answering), not against
NOMAD's feature list.

| Capability | Project NOMAD | Just In Case | Gap |
|---|---|---|---|
| Grounded answers with per-source citations | RAG via Qdrant + Ollama (`rag_service.ts`), nomic-embed-text @ 1500-token chunks | Core product. vector + BM25 → RRF, per-match filenames and scores, Sources accordion | **JIC ahead** |
| Hybrid lexical + semantic retrieval | Qdrant vector search; no lexical stage evidenced | vec0 MATCH + FTS5 BM25, RRF *k*=60 | **JIC ahead** |
| Curated, licence-cleared emergency corpus | Generic Kiwix catalogue, curated by tier not by mission | 32 sources / 8 emergency categories, every URL verified and licence recorded | **JIC ahead** |
| Container security posture | `docker.sock` into 3 containers, `/:/host:ro`, management plane on `:latest` | every service `read_only`, `cap_drop: ALL`, `no-new-privileges`, no socket | **JIC ahead** |
| Model source breadth | Ollama registry only | any GGUF (`LLM_GGUF_REPO`) | **JIC ahead** |
| **ZIM / Kiwix support** | kiwix-serve 3.8.1, ZIM manager, OPDS catalogue | **was: none** (`src/ingestion.cpp:144` accepts only `.pdf`/`.txt`) | **closed — §3** |
| **GPU acceleration** | auto-detects NVIDIA + AMD, swaps to `ollama/ollama:rocm`; publishes 35 → 216 tok/s | **was: CPU only**, `n_gpu_layers` never set | **closed — §3** |
| **Offline maps** | Protomaps/OSM PMTiles, 50 regions / 19.7 GB | **was: none** | **partly closed — §3** |
| Encyclopedic content scale | 238,655 MB across manifests (Wikipedia to 124 GB; 62 Kiwix packs) | ~350 MB local, **plus whatever ZIM is mounted** | **reframed — §4** |
| Library usable without the AI stack | Kiwix ships a Xapian index; browse + search work with Ollama absent | `/query` 503s without the LLM; `/api/library` empty without the embedding model | **open — major** |
| In-app content add/remove | setup wizard, ZIM manager, background downloads, delete | no write endpoints; `docker compose cp`. Deleting a file leaves orphaned chunks that `/query` still cites | **open — major** |
| Education platform (Kolibri/Khan) | Kolibri 0.19.4, progress tracking | one source in `600_Education` | **non-goal — §5** |
| App catalog (CyberChef, Jellyfin, Vaultwarden…) | ~12 more pinned images | none | **non-goal** |
| Hardware benchmark / leaderboard | NOMAD Score, 1,500+ submissions | none | **non-goal** |
| Auto-update engine | gated minor/patch auto-update | `docker compose pull` | minor |
| Install UX | two commands | three steps plus a build | minor |
| Authentication | none by design | none | neither |

---

## 3. What this branch closes

### ZIM library, as a third retriever

`docker compose --profile library up -d` runs upstream `kiwix-serve` over a new `jic-zim` volume;
`helper-scripts/fetch-zim.sh` fills it. `src/kiwix_client.h` then queries it **at answer time**,
and its hits join the local vector and BM25 results in the *same* Reciprocal Rank Fusion. A ZIM
article can therefore be quoted and cited in an answer.

This is the part that is not merely parity. NOMAD reaches the same capability by embedding ZIM
content into Qdrant ahead of time; JIC queries Kiwix's own Xapian index on demand. The
consequence is that JIC needs no embedding pass over 100 GB of Wikipedia to answer from it — see
§4 for why that matters more than it sounds.

**Ranking is deliberately not neutral.** A local chunk can be scored by two retrievers and tops
out near `2/(k+1)`; a ZIM hit has one and tops out near `1/(k+1)`. FM 21-76 therefore outranks a
Wikipedia article when both match. The library is reach, not the primary source.

**Licensing — the reason it is a service, not a library.** libzim is **GPL-2.0-or-later** with no
linking exception; JIC is MIT. Linking libzim into `jic-ingestion` would propagate obligations to
the binary — a licence change for the product, not an implementation detail. Talking to
`kiwix-serve` over a socket is mere aggregation. **Do not link libzim.** It buys nothing a
separate process does not.

### GPU acceleration

`--build-arg JIC_GPU=vulkan` builds ggml's Vulkan backend; `JIC_N_GPU_LAYERS` offloads at runtime.
Vulkan rather than CUDA because CUDA and ROCm each need a vendor toolchain that only exists in a
different base image, while Vulkan installs from Ubuntu's archive and covers NVIDIA, AMD and Intel
with one artifact. NOMAD solves the same problem by swapping the entire Ollama image per vendor,
which it can do because it ships no inference code of its own.

Two traps found and fixed while doing this, both of which produce a *silently* CPU-only build:

- `CMakeLists.txt` hard-coded the ggml archives to link (`base`, `cpu`, `ggml`). A GPU build emits
  an extra `libggml-vulkan.a` that would simply never be linked. The list is globbed now.
- llama.cpp ignores `n_gpu_layers` on a CPU-only build without warning. `/status` therefore
  reports the **compiled-in** backend next to the **requested** layer count, as two fields, because
  they can disagree and the disagreement is the whole diagnostic.

### Offline maps

`docker compose --profile maps up -d` serves an `.mbtiles` extract through tileserver-gl.
Deliberately **not** wired into retrieval: a map is something you look at, and implying the LLM
can read tiles would be a claim the product cannot keep.

---

## 4. The one gap that cannot be closed by trying harder

NOMAD's manifests total ~238 GB; JIC's curated corpus is ~350 MB. That is ~680×, and the naive
reading — "ingest more" — ships something worse than today's product.

Full-Wikipedia RAG is unreachable at any effort level on this architecture: `MAX_CONTEXT_CHUNKS=5`
top-*k* over a `vec0` virtual table whose MATCH is brute-force, fed by CPU nomic-embed at
`CHUNK_SIZE=1500`, against the ~50M vectors `wikipedia_all_maxi` implies. `architecture.md` §12
already anticipates this with its "pgvector escape hatch" note.

The federated design in §3 is the answer to exactly this: **do not ingest the encyclopedia, query
it.** Kiwix already maintains a Xapian full-text index over every mounted ZIM. JIC borrows it per
query and pays nothing at rest. Anyone scoping this work as "add `.zim` to the extension filter"
has misread the problem.

---

## 5. What JIC should deliberately not build

- **Kolibri / Khan Academy.** A courseware platform with progress tracking is a different product
  with a different user in a different mood. The emergency corpus already carries reference
  material; structured coursework is not what someone opens when the power is out.
- **The Supply Depot.** Jellyfin, Vaultwarden and CyberChef are a home-server catalogue. That is
  Companion Hub's job in this platform, not JIC's.
- **A benchmark leaderboard.** NOMAD Score is a community-growth mechanism, not a capability.

---

## 6. Ranked remainder

| # | Gap | Effort | Note |
|---|---|---|---|
| 1 | Library unusable without the LLM (`/query` 503s; `/api/library` needs the embedding model) | M | Has an hours-long slice: let FTS5-only search answer when no vector is available |
| 2 | No in-app content add/remove; deleting a file leaves orphaned chunks that `/query` still cites | M | `origin/claude/content-library-import` is +1178/−22 and already half-reviewed. Needs a `remove_file()` prune it lacks |
| 3 | Remote catalogue search (OPDS) for arbitrary archives | S | `fetch-zim.sh` resolves current filenames already; a `--catalog` query is a small extension |
| 4 | Install UX: three steps to NOMAD's two | S | |

---

_Related: [`architecture.md`](../architecture.md) §12, [`sources.yaml`](../sources.yaml),
[`src/kiwix_client.h`](../src/kiwix_client.h)._
