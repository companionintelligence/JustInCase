# ANTIGRAVITY.md — JustInCase

## Context

**JustInCase** — **App/Agent** layer of the Companion Intelligence platform (Lifescope Inc).

Architecture: `CI-Engineering/architecture/architecture.md`
Roadmap: `CI-Engineering/architecture/roadmap.md`

## Purpose

Offline emergency knowledge assistant. C++ server with llama.cpp backend, Tika document parsing, and a vector index. Runs via Docker Compose.

## Stack

- C++ / llama.cpp (server + ingestion)
- Apache Tika (document parsing)
- Docker Compose (jic + tika + ingestion services)
- GGUF model files (in gguf_models/)

## Commands

```bash
# download GGUF models: ./helper-scripts/fetch-models.sh
docker compose up --build
./helper-scripts/test-config.sh && ./helper-scripts/test-server.sh
```

## Rules

- Package manager: do not switch or mix.
- Destructive git operations (`reset --hard`, `clean -f`) require explicit user confirmation.
- Commits reference CI-Engineering issues.
- All content is **private and confidential** — Property of Lifescope Inc.
