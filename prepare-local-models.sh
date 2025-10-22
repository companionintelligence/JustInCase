#!/bin/bash

echo "🚀 Use Existing Local Models (NO INTERNET REQUIRED)"
echo "=================================================="
echo ""

# Get model names from config
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

echo "📋 Looking for these models in your local Ollama:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

# Check if user has local Ollama directory
if [ ! -d "$HOME/.ollama" ]; then
    echo "❌ No local Ollama directory found at ~/.ollama"
    echo "   Do you have Ollama installed locally with the models?"
    exit 1
fi

echo "✅ Found local Ollama directory"
echo ""

# Check what models exist locally
echo "📦 Your local models:"
if [ -d "$HOME/.ollama/models/manifests/registry.ollama.ai/library" ]; then
    ls -la "$HOME/.ollama/models/manifests/registry.ollama.ai/library/"
else
    echo "No models found in standard location"
fi

# Create a local directory to use as mount point
mkdir -p ./local_ollama_mount

echo ""
echo "🔗 Creating symlink to your local Ollama models..."
# Remove existing symlink/directory if it exists
rm -rf ./local_ollama_mount/.ollama

# Create symlink to user's .ollama directory
ln -s "$HOME/.ollama" ./local_ollama_mount/.ollama

echo "✅ Symlink created"
echo ""
echo "Your local models will be mounted directly into Docker."
echo "NO INTERNET CONNECTION REQUIRED!"
echo ""
echo "Next: docker compose up --build"
