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

Offline emergency knowledge assistant. C++ server with llama.cpp backend, Tika document parsing, and a vector index. Runs via Docker Compose.

## Tech stack

- C++ / llama.cpp (server + ingestion)
- Apache Tika (document parsing)
- Docker Compose (jic + tika + ingestion services)
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
