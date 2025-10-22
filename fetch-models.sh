#!/bin/bash

echo "🚀 Model Download Guide (LOCAL ONLY)"
echo "===================================="
echo ""
echo "This system requires models to be downloaded LOCALLY before use."
echo "Docker will NOT download models from the internet."
echo ""

# Get model names from config
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

echo "📋 Required models:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

echo "STEP 1: Download GGUF files manually"
echo "===================================="
echo "Download these files from HuggingFace:"
echo ""
echo "For Llama 3.2 (choose one):"
echo "  • https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF"
echo "  • https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF"
echo "  Download: Llama-3.2-1B-Instruct-Q4_K_M.gguf (or similar)"
echo ""
echo "For nomic-embed-text:"
echo "  • https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF"
echo "  Download: nomic-embed-text-v1.5.Q4_K_M.gguf"
echo ""

echo "STEP 2: Place files in ./gguf_models/"
echo "====================================="
echo "Create the directory and put your downloaded .gguf files there:"
echo "  mkdir -p gguf_models"
echo "  # Copy your downloaded files to ./gguf_models/"
echo ""

echo "STEP 3: Load models into Docker"
echo "================================"
echo "Run these commands:"
echo "  ./prepare-gguf-models.sh    # Creates Modelfiles"
echo "  docker compose up -d        # Start containers"
echo "  ./load-gguf-models.sh       # Load models (no internet needed)"
echo ""

echo "STEP 4: Verify and run"
echo "======================"
echo "Check models are loaded:"
echo "  docker compose exec ollama ollama list"
echo ""
echo "Then run the full system:"
echo "  docker compose up"
echo ""

# Check if gguf_models directory exists
if [ -d "./gguf_models" ]; then
    echo "📁 Found gguf_models directory. Contents:"
    ls -la ./gguf_models/*.gguf 2>/dev/null || echo "  No .gguf files found yet"
else
    echo "📁 No gguf_models directory found. Create it with: mkdir -p gguf_models"
fi
