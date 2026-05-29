# Model Configuration Guide

This project uses environment variables for centralized model configuration.
**Change the model configuration in one place: `docker-compose.yml`**

## Environment Variables

### Core Model Configuration
- `LLM_MODEL`: The name of the LLM model (default: `qwen2.5-vl:7b`)
- `EMBEDDING_MODEL`: The name of the embedding model (default: `nomic-embed-text`)

### GGUF File Configuration
- `LLM_GGUF_FILE`: The filename of the LLM GGUF file (default: `Qwen2.5-VL-7B-Instruct-Q4_K_M.gguf`)
- `LLM_MMPROJ_FILE`: The filename of the LLM vision projection file (default: `mmproj-Qwen2.5-VL-7B-Instruct-f16.gguf`)
- `EMBEDDING_GGUF_FILE`: The filename of the embedding GGUF file (default: `nomic-embed-text-v1.5.Q4_K_M.gguf`)

### Repository Configuration (for downloading)
- `QWEN_GGUF_REPO`: HuggingFace repo for Qwen models (default: `Qwen/Qwen2.5-VL-7B-Instruct-GGUF`)
- `NOMIC_GGUF_REPO`: HuggingFace repo for Nomic models (default: `nomic-ai/nomic-embed-text-v1.5-GGUF`)

## How to Change Models

### To switch to a different model:

1. **Edit `docker-compose.yml`** - Update the environment variables in both `jic` and `ingestion` services:
   ```yaml
   environment:
     - LLM_MODEL=your-new-model:tag
     - LLM_GGUF_FILE=your-new-model-file.gguf
     - LLM_MMPROJ_FILE=your-new-model-mmproj.gguf
     # Keep embedding model the same or change if needed
   ```

2. **Download the new GGUF file** to `./gguf_models/`

3. **Rebuild and restart**:
   ```bash
   docker compose down
   docker compose build
   docker compose up -d
   ```

### Example: Switching to a Different Qwen Variant

```yaml
environment:
  - LLM_MODEL=qwen2.5-vl:7b
  - LLM_GGUF_FILE=Qwen2.5-VL-7B-Instruct-Q4_K_M.gguf
  - LLM_MMPROJ_FILE=mmproj-Qwen2.5-VL-7B-Instruct-f16.gguf
```

## Architecture

- **C++ Server**: Reads `LLM_GGUF_FILE` and `EMBEDDING_GGUF_FILE` environment variables
- **Shell Scripts**: Use same environment variables with sensible defaults
- **Docker Compose**: Central configuration point for all services
- **No Python Dependencies**: All scripts use pure shell/bash

## Files Affected by Model Configuration

- `src/config.h` - C++ configuration functions
- `src/llm.h` - LLM model loading
- `src/embeddings.h` - Embedding model loading
- `helper-scripts/fetch-models.sh` - Download and setup
- `docker-compose.yml` - Environment variable definitions
