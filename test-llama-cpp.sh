#!/bin/bash

echo "🧪 Testing llama.cpp Docker setup"
echo "================================="

# Check if GGUF files exist
echo "Checking GGUF models..."
if [ -f "./gguf_models/$(python3 -c 'from config import LLAMA_GGUF_FILE; print(LLAMA_GGUF_FILE)')" ]; then
    echo "✅ LLM GGUF found"
else
    echo "❌ LLM GGUF not found"
    exit 1
fi

if [ -f "./gguf_models/$(python3 -c 'from config import NOMIC_GGUF_FILE; print(NOMIC_GGUF_FILE)')" ]; then
    echo "✅ Embedding GGUF found"
else
    echo "❌ Embedding GGUF not found"
    exit 1
fi

# Test with Docker
echo ""
echo "Testing llama.cpp Docker image..."
docker run --rm -v ./gguf_models:/models:ro ghcr.io/ggerganov/llama.cpp:server --help > /dev/null 2>&1

if [ $? -eq 0 ]; then
    echo "✅ Docker image is available"
else
    echo "❌ Failed to run Docker image"
    echo "Pulling image..."
    docker pull ghcr.io/ggerganov/llama.cpp:server
fi

echo ""
echo "✅ Ready to run with: docker compose up"
