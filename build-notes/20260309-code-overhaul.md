# Code Overhaul — 9 March 2026

## Context

JIC had grown organically around a stack that included Apache Tika (Java-based PDF extraction running in a separate Docker container), a hand-rolled brute-force vector index serialised to flat files, PostgreSQL with pgvector for an alternative store that was never actually wired up, and Qwen2.5-VL-7B as the default LLM. The system worked, but carried significant weight — a JVM container just to read PDFs, O(n) cosine-similarity scans over every chunk on every query, a 2048-token context window, and several pieces of dead or duplicated code.

This overhaul replaced all of that with a leaner, pure-C++ stack while preserving the same external interface (HTTP API and web frontend).

## What changed

### PDF extraction: Apache Tika → MuPDF

Tika required a separate Docker container running a JVM, an HTTP round-trip for every document, and curl linked into the C++ binary. It has been replaced with MuPDF (pinned to 1.27.2), a small C library from Artifex that extracts text directly in-process. A new header, `pdf_utils.h`, wraps the fz_context / fz_document / fz_stext_page API and exposes two functions: `extract_pdf_pages()` (returns a vector of page-number / text pairs) and `extract_pdf_text()` (concatenated string). The Tika Dockerfile, the cache-tika helper script, and all curl dependencies in the ingestion path were deleted.

### Search index: flat-file vector scan → SQLite + sqlite-vec + FTS5

The old `SimpleVectorIndex` held every embedding in a `std::vector`, serialised it to a binary file, and did a linear cosine-similarity scan on every query. It also maintained a parallel `metadata.jsonl` file for chunk metadata, which had to be kept in sync manually. The replacement is a single SQLite database (`data/jic.db`) with three backing structures: a `vec0` virtual table via sqlite-vec for approximate nearest-neighbour vector search, an FTS5 virtual table for BM25 full-text ranking, and a plain table for chunk metadata and processed-file tracking. Query results from both indexes are merged using Reciprocal Rank Fusion (RRF, k=60), which consistently outperforms either index alone. The old `pg_vector_store.h` (a PostgreSQL/pgvector stub that was never connected) was also deleted.

### Chunking: fixed-size split → recursive semantic splitter

The previous chunker split on double-newlines and then by a fixed character count with no overlap. The new `text_utils.h` implements a recursive splitter that tries a hierarchy of separators — paragraph breaks, line breaks, sentence-ending punctuation, commas, then spaces — falling back to the next-finer granularity only when a chunk would exceed the target size (1500 characters). Chunks overlap by 200 characters to avoid losing context at boundaries, and anything under 80 characters is discarded.

### LLM: Qwen2.5-VL-7B → Llama 3.2 3B Instruct

The 7B vision-language model was overkill for a text-only Q&A system and required roughly 6 GB of RAM. The default is now Llama 3.2 3B Instruct (Q4_K_M quantisation, ~2 GB), which is faster, fits comfortably on 8 GB machines, and produces good answers for this use case. The vision projector model (`mmproj-Qwen2.5-VL-7B-Instruct-f16.gguf`) is no longer referenced. The context window was widened from 2048 to 8192 tokens, and the context-stuffing budget was raised from 800 to 6000 characters, so the LLM now sees substantially more source material per query.

### llama.cpp: unpinned master → pinned b8250 at ggml-org

The Dockerfile was previously cloning `master` from `ggerganov/llama.cpp`, meaning builds were not reproducible. It is now pinned to tag `b8250` from the renamed `ggml-org/llama.cpp` organisation.

### HTTP server: raw sockets → cpp-httplib

The original server hand-rolled HTTP on raw POSIX sockets — custom request parsing, manual header construction, content-type guessing, a thread-per-connection accept loop, and a hand-built rate limiter. All of that (roughly 250 lines) was replaced with [cpp-httplib](https://github.com/yhirose/cpp-httplib), a single-header C++ HTTP library. It provides routing, a thread pool, static file mounting with automatic MIME detection, keep-alive, and request size limits out of the box. The server went from 509 lines to 266.

### Build system

CMakeLists.txt was rewritten. It links against the pre-built llama.cpp and MuPDF static libraries from the Docker build stages, compiles the SQLite and sqlite-vec amalgamations with FTS5 enabled, and pulls nlohmann/json as a single header. The curl and OpenBLAS dependencies were dropped. The Dockerfile was consolidated from a tangle of separate Dockerfiles (Dockerfile, Dockerfile.tika, Dockerfile.prebuilt) into a single four-stage build: llama-builder, mupdf-builder, app-builder, and a slim runtime image. A `HEALTHCHECK` was added.

### Shared types

The `Document` struct was defined identically in both `server.cpp` and `pg_vector_store.h`. It now lives in a single `types.h` header.

### Server fixes

CORS was hardcoded to `https://example.com`, which broke every real deployment. Changed to `Access-Control-Allow-Origin: *`. The `/status` endpoint was re-reading the entire index from disk every five seconds; it now caches chunk and file counts in `std::atomic<int>` variables refreshed by a background thread every ten seconds. The LLM system prompt was retuned for emergency-preparedness context, and the temperature was dropped from 0.8 to 0.7.

### Frontend

`chat.js` gained a lightweight `renderMarkdown()` function that converts bold, italic, code blocks, inline code, headings, and lists to HTML, so LLM responses are no longer dumped as raw text.

### Data sourcing

A `sources.yaml` manifest was added listing roughly eighteen public-domain and freely-redistributable emergency documents with direct download URLs, categories, and license information. The `fetch-models.sh` helper was rewritten to download the two new default models.

## Files deleted

`pg_vector_store.h`, `simple_vector_index.h`, `Dockerfile.tika`, `Dockerfile.prebuilt`, `README-docker.md`, `helper-scripts/cache-tika.sh`.

## Files created

`src/types.h`, `src/pdf_utils.h`, `src/sqlite_vec_index.h`, `sources.yaml`.

## Files rewritten

`src/config.h`, `src/text_utils.h`, `src/embeddings.h`, `src/llm.h`, `src/ingestion.cpp`, `src/server.cpp`, `CMakeLists.txt`, `Dockerfile`, `docker-compose.yml`, `helper-scripts/fetch-models.sh`, `README.md`.

## Files modified

`public/chat.js` (added markdown rendering).

## What remains

The fetch script for downloading source PDFs listed in `sources.yaml` is not yet written — several of the listed URLs turned out to redirect to landing pages or reject non-browser user agents, so the manifest will need some URL fixes before automation is reliable. A Docker build has not yet been tested end-to-end against the new pinned versions. The thread-per-connection HTTP server and global mutable state are known limitations carried forward from the original code.
