#!/bin/bash

echo "🚀 Prepare Local Models for Docker"
echo "=================================="
echo "This script copies models from ~/.ollama to ./ollama_models"
echo ""

# Get model names from config
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

echo "📋 Required models:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

# Check if local Ollama directory exists
if [ ! -d "$HOME/.ollama" ]; then
    echo "❌ No local Ollama installation found at ~/.ollama"
    echo "   Please install Ollama and pull the required models first:"
    echo "   ollama pull $LLM_MODEL"
    echo "   ollama pull $EMBEDDING_MODEL"
    exit 1
fi

# Create target directory
mkdir -p ollama_models

# Copy the entire .ollama structure
echo "📦 Copying models from ~/.ollama to ./ollama_models..."
cp -r ~/.ollama/* ./ollama_models/ 2>/dev/null || true

# Verify models exist
echo ""
echo "🔍 Verifying models..."
MISSING_MODELS=()

# Check for LLM model
if [ -d "./ollama_models/models/manifests/registry.ollama.ai/library/$LLM_MODEL" ]; then
    echo "✅ Found $LLM_MODEL"
else
    echo "❌ Missing $LLM_MODEL"
    MISSING_MODELS+=("$LLM_MODEL")
fi

# Check for embedding model
if [ -d "./ollama_models/models/manifests/registry.ollama.ai/library/$EMBEDDING_MODEL" ]; then
    echo "✅ Found $EMBEDDING_MODEL"
else
    echo "❌ Missing $EMBEDDING_MODEL"
    MISSING_MODELS+=("$EMBEDDING_MODEL")
fi

if [ ${#MISSING_MODELS[@]} -gt 0 ]; then
    echo ""
    echo "❌ Missing models: ${MISSING_MODELS[*]}"
    echo ""
    echo "Please pull these models locally first:"
    for model in "${MISSING_MODELS[@]}"; do
        echo "  ollama pull $model"
    done
    exit 1
fi

echo ""
echo "✅ All required models are available!"
echo "📁 Models prepared in: ./ollama_models"
echo ""
echo "You can now run: docker compose up --build"
