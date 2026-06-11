# Just In Case (JIC) - System Architecture

> The canonical, diagrammed implementation reference is
> [architecture.md](../architecture.md) at the repo root. This document keeps
> the design narrative and rationale.

## Reviewing JIC

Just In Case is an emergency knowledge assistant that provides conversational access to a collection of PDF documents through a modern AI-powered interface. The system is designed to be self-contained, efficient, and deployable in resource-constrained environments where internet connectivity may be limited or unavailable.

## Reviewing RAG

Modern large-language-model systems rely on vector representations to bridge the gap between unstructured text and computational reasoning. At the foundation is a document store—a repository that holds source material such as reports, articles, and transcripts, broken into manageable chunks. Each chunk is converted into a dense vector, a high-dimensional numerical embedding that encodes the semantic meaning of the text. These vectors make it possible to compare passages not by keywords but by conceptual similarity, allowing the system to recall relevant context even when the phrasing differs entirely from the user’s query.

To generate these embeddings, open-source models such as Nomic-Embed, E5, or GTE are widely used. They are lightweight transformer encoders trained to map sentences into a shared semantic space where related ideas cluster together. Nomic’s model, in particular, is optimized for general retrieval across domains and languages, offering both quality and speed for local deployments. The resulting vectors are stored in a vector index that supports approximate nearest-neighbor search with millisecond latency.

When a query arrives, the system encodes it into its own vector and searches the index for the closest matches. The top-ranked chunks are then optionally re-ranked using a more precise cross-encoder or a lightweight LLM to refine relevance. The highest-scoring passages are concatenated with the user’s question to form the prompt context passed to the model. This retrieval-augmented process effectively expands the model’s working memory beyond its fixed context window, allowing it to reason over large document collections while staying grounded in verifiable sources.

Finally, a carefully designed pre-prompt or system instruction establishes the model’s behavior—directing it to use retrieved material faithfully, cite evidence, or adopt a particular tone. Together, these elements—document storage, vector embeddings, fast similarity search, reranking, and prompt construction—form the backbone of modern retrieval-augmented generation (RAG) pipelines that extend LLMs from generic text generators into targeted, knowledge-grounded assistants.

## Implementation approach here: Core Architecture Principles

The architecture follows a "pure C++" approach, directly binding to the llama.cpp library without intermediate layers or language bindings. This design choice prioritizes performance, minimal dependencies, and deployment simplicity. The entire system runs as containerized services, making it portable across different environments while maintaining consistent behavior.

A second principle: **the image is code, the volumes are state**. The container image carries only the two binaries and the web UI. The document library, the search index, and the GGUF models are provisioned at runtime (named volumes for library and index, a read-only bind mount for models). The library can be updated, re-fetched, or swapped without ever rebuilding the image, and the image can be republished without touching anyone's data.

## Implementation approach: Key Components

### Document Processing Pipeline

PDF text extraction is done in-process with **MuPDF** — no JVM, no external parsing service, no network round-trips. The ingestion worker scans the sources volume on a fixed cadence (default 30 s), skips hidden and still-settling files (the content fetcher downloads to `*.part` and renames atomically, so partial documents are never ingested), extracts text page by page, and splits it with a recursive paragraph → sentence → word splitter into overlapping chunks (~1500 chars, 200 overlap) that respect natural document structure.

### Embedding Generation

The system uses Nomic Embed Text v1.5, an embedding model optimized for semantic search. Running directly through llama.cpp with GGUF quantized models, it generates 768-dimensional vectors for each text chunk. These embeddings capture the semantic meaning of the text, enabling the system to find relevant passages based on meaning rather than just keyword matching.

### Vector Storage and Hybrid Retrieval

Everything lives in a single SQLite database file: chunk text and metadata in ordinary tables, vectors in a [sqlite-vec](https://github.com/asg017/sqlite-vec) `vec0` virtual table, and a full-text index in FTS5. At query time the question is run against **both** indexes — approximate nearest-neighbour over the embeddings and BM25 over the text — and the two ranked lists are merged with Reciprocal Rank Fusion. Hybrid retrieval catches both semantic matches ("stop the bleeding" → hemorrhage control) and exact-term matches (drug names, model numbers) that pure vector search misses. One file, zero external services, WAL mode for safe concurrent access from the server and the ingestion worker.

### Language Model Integration

The conversational interface defaults to Llama 3.2 3B Instruct (Q4_K_M), chosen for its balance of capability and resource efficiency on 8 GB-class hardware; any GGUF instruction-tuned model can be swapped in via `LLM_GGUF_FILE`. The model runs directly through llama.cpp using the model's own chat template, with a system prompt that directs it to answer from the retrieved references and cite them. If the models are absent the server starts in degraded mode: UI, status, and library remain available while `/query` answers 503.

### Web Interface

A clean, dependency-free web interface (vanilla JS + CSS) provides the user-facing experience: dark/light themes, a live library panel showing exactly what is indexed, conversation history, source citations with relevance scores, and a context toggle for direct LLM conversations. There are no inline scripts or handlers, so the server can enforce a strict Content-Security-Policy.

## Architectural Decisions

### Why Pure C++?

The decision to use C++ throughout the stack eliminates the overhead of language bindings and ensures maximum performance. By directly linking against llama.cpp, we avoid the complexity and potential instability of Python bindings or HTTP APIs. This approach also reduces memory usage and startup time, critical factors in emergency scenarios.

### SQLite Hybrid Search vs. FAISS / pgvector

Earlier iterations used a custom in-memory vector store and considered PostgreSQL + pgvector. SQLite with sqlite-vec + FTS5 replaced both: it adds BM25 lexical search (which pure vector stores lack), persists incrementally instead of rewriting a binary blob, survives crashes via WAL, and keeps the offline-appliance promise — the whole index is one file on one volume. pgvector remains a credible escape hatch if collections grow past what a single SQLite file serves well.

### Content in Volumes, Not the Image

Knowledge content is deliberately excluded from the container image (`.dockerignore` blocks `public/sources/`). A one-shot `content-fetch` service — enabled with `docker compose --profile fetch` — seeds the sources volume with any repo-committed starter documents and downloads the curated manifest (`sources.yaml`), verifying file type and optional SHA-256 before atomically publishing each file. This keeps images small and reproducible, makes the library independently updatable, and keeps licensing obligations visible (the manifest records the license of every document).

### Separation of Ingestion and Serving

Running document ingestion as a separate service prevents long-running PDF processing from impacting query response times. This approach also allows for different resource allocations — the ingestion service is CPU-limited while the main server gets priority access to system resources. Both processes share the SQLite database through WAL mode.

### Security Posture

Containers run as a non-root user on a read-only root filesystem with all capabilities dropped and `no-new-privileges` set; only the data volumes are writable. The server validates and bounds all input (1 MB body cap, 8 000-char queries, sanitized conversation IDs), sends strict security headers (CSP on HTML, nosniff, frame denial), keeps CORS off unless explicitly configured, and shuts down cleanly on SIGTERM.

## Data Flow

1. **Library provisioning**: the content fetcher (or the operator) places PDFs/TXTs into the `jic-sources` volume
2. **Discovery**: the ingestion worker notices new, settled files on its next scan
3. **Text Extraction**: MuPDF extracts text in-process
4. **Chunking**: text is split into overlapping, structure-aware chunks
5. **Embedding**: each chunk becomes a 768-dimensional vector
6. **Indexing**: chunks land in SQLite — vec0 for vectors, FTS5 for text
7. **Query Processing**: questions are embedded; vector + BM25 results are fused (RRF)
8. **Response Generation**: top chunks ground the LLM's answer
9. **Citation Display**: sources are shown with relevance scores and links to the original documents

## Deployment Considerations

The system ships as a single container image used by three compose services: the server, the ingestion worker, and the opt-in content fetcher. State is held in two named volumes (`jic-sources`, `jic-data`) plus a host directory of GGUF models mounted read-only. The use of GGUF models allows for efficient CPU-only deployment, making the system accessible on a wide range of hardware without requiring GPUs. Health is monitored via `/status`; CI gates publication on unit tests, static config lint, an end-to-end server test, and a UI screenshot check.

## Future Enhancements

The modular design makes it straightforward to swap components — replacing the embedding model, upgrading the LLM as hardware allows, or moving the index to pgvector if collections outgrow SQLite. Candidate next steps: page-level citation anchors (MuPDF already yields per-page text), incremental re-indexing on file change (hash-based), Kiwix ZIM ingestion for offline Wikipedia, and map tiles served alongside the library.
