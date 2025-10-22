#!/bin/bash

echo "🔄 Copy Models to Local Directory"
echo "================================="
echo ""
echo "This copies your Ollama models to a local directory we control."
echo ""

# Check if user has models
if [ ! -d "$HOME/.ollama/models" ]; then
    echo "❌ No models found at ~/.ollama/models"
    exit 1
fi

echo "📦 Found models at ~/.ollama/models"
echo ""

# Create local directory
mkdir -p ./ollama_local

# Copy entire .ollama structure
echo "📋 Copying models (this may take a moment)..."
cp -r ~/.ollama/* ./ollama_local/

echo ""
echo "✅ Models copied to ./ollama_local"
echo ""
echo "📊 Size: $(du -sh ./ollama_local | cut -f1)"
echo ""
echo "Now update docker-compose.yml to use ./ollama_local instead of \${HOME}/.ollama"
