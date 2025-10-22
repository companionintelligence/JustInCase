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

# Check if local Ollama is installed
if ! command -v ollama &> /dev/null; then
    echo "❌ Ollama is not installed locally!"
    echo "   Please install Ollama first: https://ollama.ai/download"
    exit 1
fi

# Check if local Ollama directory exists
if [ ! -d "$HOME/.ollama" ]; then
    echo "⚠️  No local Ollama directory found at ~/.ollama"
    echo "   Creating it now..."
    mkdir -p "$HOME/.ollama"
fi

# Check and pull missing models locally
echo "🔍 Checking local models..."
NEED_PULL=false

# Check for LLM model
if ollama list 2>/dev/null | grep -q "^$LLM_MODEL"; then
    echo "✅ $LLM_MODEL already available locally"
else
    echo "📥 $LLM_MODEL not found locally, pulling..."
    NEED_PULL=true
    ollama pull "$LLM_MODEL"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to pull $LLM_MODEL"
        exit 1
    fi
fi

# Check for embedding model
if ollama list 2>/dev/null | grep -q "^$EMBEDDING_MODEL"; then
    echo "✅ $EMBEDDING_MODEL already available locally"
else
    echo "📥 $EMBEDDING_MODEL not found locally, pulling..."
    NEED_PULL=true
    ollama pull "$EMBEDDING_MODEL"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to pull $EMBEDDING_MODEL"
        exit 1
    fi
fi

if [ "$NEED_PULL" = true ]; then
    echo ""
    echo "⏳ Waiting for models to be fully registered..."
    sleep 3
fi

# Create target directory
mkdir -p ollama_models

# Copy the entire .ollama structure
echo ""
echo "📦 Copying models from ~/.ollama to ./ollama_models..."
cp -r ~/.ollama/* ./ollama_models/ 2>/dev/null || true

# Verify models were copied
echo ""
echo "🔍 Verifying copied models..."
MISSING_MODELS=()

# Check for LLM model
if [ -d "./ollama_models/models/manifests/registry.ollama.ai/library/$LLM_MODEL" ]; then
    echo "✅ $LLM_MODEL copied successfully"
else
    echo "❌ $LLM_MODEL not found after copy"
    MISSING_MODELS+=("$LLM_MODEL")
fi

# Check for embedding model
if [ -d "./ollama_models/models/manifests/registry.ollama.ai/library/$EMBEDDING_MODEL" ]; then
    echo "✅ $EMBEDDING_MODEL copied successfully"
else
    echo "❌ $EMBEDDING_MODEL not found after copy"
    MISSING_MODELS+=("$EMBEDDING_MODEL")
fi

if [ ${#MISSING_MODELS[@]} -gt 0 ]; then
    echo ""
    echo "❌ Failed to copy models: ${MISSING_MODELS[*]}"
    echo "   This might be a permission issue or the models weren't fully downloaded."
    exit 1
fi

echo ""
echo "✅ All required models are available!"
echo "📁 Models prepared in: ./ollama_models"
echo ""
echo "You can now run: docker compose up --build"
