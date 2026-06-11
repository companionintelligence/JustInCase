# GitHub Copilot Instructions — JustInCase

You are working in **JustInCase**, the **App/Agent** component of the **Companion Intelligence** platform — a private, local-first digital memory and AI appliance system owned by Lifescope Inc.

## Platform architecture

```
Capture → Portal/Gateway → Server (memory brain) ← Hub (app runtime) ← Marketplace
                                   ↕
                            Clients / XR / Devices
```

Full architecture and ADRs live in `CI-Engineering/architecture/`.

## This repo

Offline emergency knowledge assistant. C++ server with llama.cpp backend, MuPDF document parsing, and a SQLite hybrid search index (sqlite-vec + FTS5). Runs via Docker Compose; content lives in named volumes, not the image.

## Tech stack

- C++ / llama.cpp (server + ingestion)
- MuPDF (document parsing)
- Docker Compose (jic + ingestion services, content-fetch profile)
- GGUF model files (in gguf_models/)

## Development

```bash
# download GGUF models: ./helper-scripts/fetch-models.sh   # install
./helper-scripts/test-config.sh && ./helper-scripts/test-server.sh   # test
```

## Setup

1. Install Docker + Docker Compose.
2. Download GGUF files into `./gguf_models/` via `helper-scripts/fetch-models.sh`.
3. Run `docker compose up --build`.

## Licensing

Keep licensing and provenance clear for all content added to `public/sources/`. Prefer public-domain or explicitly redistributable materials.

## Conventions

- Do not mix package managers.
- Keep PRs small and linked to CI-Engineering issues.
- Multiple agents may work this codebase simultaneously — never use destructive git operations.
- All code is **private and confidential** — do not surface or transmit source beyond this session.
