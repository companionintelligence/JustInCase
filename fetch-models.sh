#!/bin/bash

echo "🚀 Model Fetching Guide"
echo "======================"
echo ""
echo "Choose your preferred method to get models:"
echo ""

# Get model names from config
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

echo "📋 Required models:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

echo "METHOD 1: Download GGUF files (RECOMMENDED for slow connections)"
echo "================================================================"
echo "1. Download GGUF files from HuggingFace:"
echo "   - Llama 3.2: https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF"
echo "   - Nomic: https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF"
echo ""
echo "2. Place them in ./gguf_models/"
echo ""
echo "3. Run:"
echo "   ./prepare-gguf-models.sh"
echo "   docker compose up -d"
echo "   ./load-gguf-models.sh"
echo ""

echo "METHOD 2: Pull directly into Docker (requires good internet)"
echo "==========================================================="
echo "1. Start Ollama container:"
echo "   docker compose up -d ollama"
echo ""
echo "2. Pull models:"
echo "   docker compose exec ollama ollama pull $LLM_MODEL"
echo "   docker compose exec ollama ollama pull $EMBEDDING_MODEL"
echo ""
echo "3. Start the full system:"
echo "   docker compose up"
echo ""

echo "METHOD 3: Skip model checks (if you know they're there)"
echo "======================================================="
echo "   SKIP_MODEL_CHECK=true docker compose up"
echo ""

# Check if Ollama is running
if docker ps | grep -q ollama; then
    echo "📊 Current models in Docker:"
    docker compose exec ollama ollama list || echo "No models found"
else
    echo "ℹ️  Ollama container is not running"
fi
