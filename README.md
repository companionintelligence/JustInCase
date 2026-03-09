# Just In Case

**Offline emergency knowledge search.**

[Live demo](https://jic.companionintel.com) · [CompanionIntelligence.com/JIC](https://companionintelligence.com/JIC) · [Discord](https://discord.gg/companion)

JIC is a self-contained, LLM-powered conversational search engine that runs entirely offline on commodity hardware. You feed it emergency PDFs — survival guides, medical references, agricultural manuals, engineering resources — and it lets you ask questions in natural language and get grounded, source-attributed answers. No cloud, no network, no JVM, no Python. Just C++ and a few GGUF model files.

> **Research project.** Do not rely on this for real-world use at this time. Use at your own risk.

---

## Why

We lean heavily on ChatGPT, Claude, Google, and similar services for even simple practical questions — _how do I wire batteries to a solar panel_, _what causes lower-right abdominal pain_. During a prolonged crisis those services may not be available. JIC bridges that gap: an offline conversational interface to actionable emergency knowledge that fits on a single machine.

Background thinking on the problem space is in the [docs/](docs/) folder: [typical emergency questions](docs/1100-questions.md), [user personas](docs/1200-persona.md), [data categorisation](docs/1300-categorization.md), [high-value sources](docs/1400-sources.md), [target hardware](docs/1500-hardware.md), and [architecture notes](docs/1600-architecture.md).

---

## How it works

The system is written in C++17 and compiles to two binaries: a server and an ingestion worker. Both link against [llama.cpp](https://github.com/ggml-org/llama.cpp) (pinned to `b8250`) for LLM inference and embeddings, [MuPDF](https://github.com/ArtifexSoftware/mupdf) (`1.27.2`) for PDF text extraction, and SQLite with [sqlite-vec](https://github.com/asg017/sqlite-vec) and FTS5 for hybrid search. The server uses [cpp-httplib](https://github.com/yhirose/cpp-httplib) for HTTP. The default LLM is Llama 3.2 3B Instruct (Q4_K_M, ~2 GB); embeddings use nomic-embed-text-v1.5 (768-dimensional, ~260 MB).

During ingestion, each PDF is parsed with MuPDF, split into chunks using a recursive paragraph → sentence → word splitter, embedded, and stored in a single SQLite database. At query time the user's question is embedded and run against both the vector index (sqlite-vec, approximate nearest neighbours) and the full-text index (FTS5 / BM25). Results are merged with Reciprocal Rank Fusion, and the top chunks are passed as context to the LLM, which produces an answer grounded in the source material.

Docker is used only for packaging — there is no runtime dependency on it. You can build and run natively if you prefer.

---

## Quickstart

You need [Docker and Docker Compose](https://docs.docker.com/get-docker/), roughly 4 GB of disk for models, and whatever space your PDF library requires.

### 1. Get source data

JIC does not ship with data. Place `.pdf` or `.txt` files into `public/sources/` — the ingestion service scans recursively, so organise them however you like. A curated manifest of public-domain emergency documents is listed in [sources.yaml](sources.yaml); a companion data repository is available at [github.com/companionintelligence/JustInCase](https://github.com/companionintelligence/JustInCase/tree/main/public/sources). You may also find the community [Survival-Data](https://github.com/PR0M3TH3AN/Survival-Data) repo useful.

### 2. Download models

Models must be present before starting Docker — they are not fetched at runtime.

```bash
./helper-scripts/fetch-models.sh
```

This places two GGUF files in `./gguf_models/`: `Llama-3.2-3B-Instruct-Q4_K_M.gguf` (~2.0 GB, the LLM) and `nomic-embed-text-v1.5.Q4_K_M.gguf` (~260 MB, the embedding model).

### 3. Build and run

```bash
docker compose up --build
```

The multi-stage Docker build compiles everything from source, then starts the server on port 8080 and the ingestion worker alongside it. Once models and sources are loaded, no internet connection is required.

The web UI is at [http://localhost:8080](http://localhost:8080). You can also query the API directly:

```bash
curl -s -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"query": "How do I purify water in the wild?"}'
```

---

## Configuration

To swap the LLM, set `LLM_GGUF_FILE` in the environment or edit `docker-compose.yml`. Any GGUF-format instruction-tuned model should work. A few reasonable options for different hardware budgets:

| Model | Parameters | RAM | Notes |
|---|---|---|---|
| **Llama 3.2 3B** (default) | 3B | ~3 GB | Fast, good quality, fits comfortably in 8 GB |
| Phi-4-mini | 3.8B | ~3.5 GB | Strong reasoning for its size |
| Gemma 3 4B | 4B | ~4 GB | Broad general knowledge |
| Llama 3.1 8B | 8B | ~6 GB | Better answers, needs ≥16 GB RAM |

---

## Screenshot

![screenshot](screenshot.png?raw=true "screenshot")

---

## Contributing

Work in progress — contributions welcome. See the [Discord](https://discord.gg/companion) for discussion.

## License

See [LICENSE](LICENSE).
