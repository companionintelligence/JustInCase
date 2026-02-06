# Codex Agent Guide

## Repo Summary
- Offline emergency knowledge assistant built in C++ with llama.cpp.
- Docker Compose runs `tika`, `jic` (server), and `ingestion`.
- Documents live under `public/sources` and are ingested into a vector index.

## Key Paths
- `docker-compose.yml`: primary model configuration and service wiring.
- `src/`: C++ server and ingestion pipeline.
- `public/sources/`: PDFs and other content to ingest.
- `helper-scripts/`: scripts for model fetching and basic testing.

## Local Setup
1. Install Docker + Docker Compose.
2. Download GGUF files into `./gguf_models/` (see `helper-scripts/fetch-models.sh`).
3. Run: `docker compose up --build`.

## Tests (Local)
- Config sanity: `./helper-scripts/test-config.sh`
- Server smoke test (server must be running): `./helper-scripts/test-server.sh`

## Remote / CI Guidance
- Minimal CI should run `./helper-scripts/test-config.sh`.
- Optional CI step (if services can be started): `docker compose up -d` then `./helper-scripts/test-server.sh`, then `docker compose down`.

## Data Notes
- Keep licensing and provenance clear for all added sources.
- Prefer public-domain or explicitly redistributable materials.
