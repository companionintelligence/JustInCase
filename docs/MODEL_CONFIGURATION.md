# Model Configuration Guide

JIC loads two GGUF models from `./gguf_models/` (bind-mounted read-only into
the containers). Configuration is environment-driven — **change models in one
place: `docker-compose.yml`** — and every consumer (C++ binaries, shell
scripts) reads the same variables.

## Environment variables

| Variable | Default | Meaning |
|---|---|---|
| `LLM_GGUF_FILE` | `Llama-3.2-3B-Instruct-Q4_K_M.gguf` | LLM file name inside `gguf_models/` |
| `EMBEDDING_GGUF_FILE` | `nomic-embed-text-v1.5.Q4_K_M.gguf` | Embedding model file name |
| `LLM_MODEL` | `llama3.2:3b` | Display name (shown in `/status` and the UI) |
| `EMBEDDING_MODEL` | `nomic-embed-text` | Display name |
| `LLM_GGUF_REPO` | `bartowski/Llama-3.2-3B-Instruct-GGUF` | HuggingFace repo `fetch-models.sh` downloads from |
| `NOMIC_GGUF_REPO` | `nomic-ai/nomic-embed-text-v1.5-GGUF` | HuggingFace repo for the embedding model |

> ⚠️ The embedding model defines the vector space. Changing
> `EMBEDDING_GGUF_FILE` to a model with different weights or dimensions
> (`EMBEDDING_DIM` is 768 in `src/config.h`) invalidates the existing index —
> wipe the `jic-data` volume and re-ingest.

## How to change the LLM

1. **Edit `docker-compose.yml`** — the `x-jic-env` anchor feeds both services:

   ```yaml
   x-jic-env: &jic-env
     LLM_MODEL: phi-4-mini
     LLM_GGUF_FILE: Phi-4-mini-instruct-Q4_K_M.gguf
     # embedding model unchanged
   ```

2. **Download the GGUF** into `./gguf_models/` — either set
   `LLM_GGUF_REPO`/`LLM_GGUF_FILE` and run `./helper-scripts/fetch-models.sh`,
   or place the file there manually.

3. **Restart** (no rebuild needed — models are not part of the image):

   ```bash
   docker compose up -d
   ```

Any GGUF instruction-tuned model works; suggested options per hardware budget
are tabulated in the [README](../README.md#configuration).

## Resource fit

| Model | File size | RAM in use | Notes |
|---|---|---|---|
| Llama 3.2 3B Q4_K_M (default) | ~2.0 GB | ~3 GB | Comfortable on 8 GB hosts |
| Phi-4-mini Q4_K_M | ~2.4 GB | ~3.5 GB | Strong reasoning for its size |
| Gemma 3 4B Q4_K_M | ~2.5 GB | ~4 GB | Broad general knowledge |
| Llama 3.1 8B Q4_K_M | ~4.9 GB | ~6 GB | Needs the compose memory limit raised |

The compose file caps the server at 8 GB — raise
`services.jic.deploy.resources.limits.memory` for larger models.

## Where the variables are consumed

| File | Role |
|---|---|
| `src/config.h` | C++ accessors (`get_llm_model_path()` etc.) with the same defaults |
| `src/llm.h` / `src/embeddings.h` | Model loading via llama.cpp |
| `helper-scripts/fetch-models.sh` | Downloads using the same variables |
| `helper-scripts/test-config.sh` | CI check that compose ↔ scripts ↔ config.h agree |
| `docker-compose.yml` | The single place you edit |
