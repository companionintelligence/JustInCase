# AGENTS.md — JustInCase

## Platform context

JustInCase is the **App/Agent** layer component of the Companion Intelligence platform — a private, local-first digital memory system.

Full architecture: `CI-Engineering/architecture/architecture.md`

## Repo summary

Offline emergency knowledge assistant. C++ server with llama.cpp backend, Tika document parsing, and a vector index. Runs via Docker Compose.

## Stack

- C++ / llama.cpp (server + ingestion)
- Apache Tika (document parsing)
- Docker Compose (jic + tika + ingestion services)
- GGUF model files (in gguf_models/)

## Setup & commands

```bash
# download GGUF models: ./helper-scripts/fetch-models.sh   # install
docker compose up --build   # dev
./helper-scripts/test-config.sh && ./helper-scripts/test-server.sh   # test
docker compose build   # build
```

## Setup

1. Install Docker + Docker Compose.
2. Download GGUF files into `./gguf_models/` via `helper-scripts/fetch-models.sh`.
3. Run `docker compose up --build`.

## Licensing

Keep licensing and provenance clear for all content added to `public/sources/`. Prefer public-domain or explicitly redistributable materials.

## Git discipline

- Never `git reset --hard` or `git clean -f` without user confirmation.
- Commit messages: imperative mood, ≤ 72 chars.
- Reference CI-Engineering issues in PRs (e.g. `Closes companionintelligence/CI-Engineering#32`).

## Confidentiality

Private & Confidential — Property of Lifescope Inc. Do not distribute.
