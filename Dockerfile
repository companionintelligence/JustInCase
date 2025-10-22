# syntax=docker/dockerfile:1
FROM python:3.11-slim-bookworm

WORKDIR /app

# Copy requirements first for better layer caching
COPY requirements.txt .

# Use pip cache mount for faster rebuilds
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install -r requirements.txt

# Copy pre-downloaded Ollama models
# This will fail the build if models don't exist
COPY ollama_models_local /root/.ollama

# Verify models are present during build
RUN if [ ! -d "/root/.ollama/models" ] || [ -z "$(ls -A /root/.ollama/models 2>/dev/null)" ]; then \
        echo "ERROR: Ollama models not found! Run ./download-models-local.sh first"; \
        exit 1; \
    fi

# Copy application code
COPY . .

ENTRYPOINT ["python3", "start.py"]
