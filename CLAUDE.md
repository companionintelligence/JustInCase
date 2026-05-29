# CLAUDE.md — JustInCase

> **Private & Confidential — Property of Lifescope Inc. Do not distribute.**

## Platform role

**JustInCase** sits in the **App/Agent** layer of the Companion Intelligence platform.

Architecture: `CI-Engineering/architecture/architecture.md`
Roadmap: `CI-Engineering/architecture/roadmap.md`
Issues: <https://github.com/companionintelligence/CI-Engineering/issues>

## What this repo does

Offline emergency knowledge assistant. C++ server with llama.cpp backend, Tika document parsing, and a vector index. Runs via Docker Compose.

## Stack

- C++ / llama.cpp (server + ingestion)
- Apache Tika (document parsing)
- Docker Compose (jic + tika + ingestion services)
- GGUF model files (in gguf_models/)

## Key commands

```bash
# Install dependencies
# download GGUF models: ./helper-scripts/fetch-models.sh

# Start development server / service
docker compose up --build

# Run tests
./helper-scripts/test-config.sh && ./helper-scripts/test-server.sh

# Build for production
docker compose build
```

## Package manager

**docker** — use exclusively; do not mix with npm/yarn/pnpm/bun unless the repo explicitly requires it.

## Cross-repo connections

- Standalone — reads from public/sources/ (PDFs)
- No live CI-Server dependency; designed for offline use

## Setup

1. Install Docker + Docker Compose.
2. Download GGUF files into `./gguf_models/` via `helper-scripts/fetch-models.sh`.
3. Run `docker compose up --build`.

## Licensing

Keep licensing and provenance clear for all content added to `public/sources/`. Prefer public-domain or explicitly redistributable materials.

## Multi-agent safety

Multiple AI agents work these repos in parallel. Never use `git reset --hard`, `git clean -f`, or any destructive git command without explicit confirmation from the user.

## Confidentiality

Private & Confidential — Property of Lifescope Inc. Do not distribute.
