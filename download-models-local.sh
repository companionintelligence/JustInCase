#!/bin/bash

# This script downloads Ollama models to your local machine
# Run this BEFORE building the Docker image

echo "🚀 Local Ollama Model Downloader"
echo "================================"
echo "This will download models to ./ollama_models for inclusion in Docker build"
echo ""

# Create the models directory
mkdir -p ollama_models_local

# Check if models already exist
if [ -d "ollama_models_local/models" ] && [ "$(ls -A ollama_models_local/models 2>/dev/null)" ]; then
    echo "✅ Models directory already exists and contains files."
    echo "   Contents:"
    find ollama_models_local -name "*.bin" -o -name "*.json" | head -10
    echo ""
    read -p "Models seem to exist. Skip download? (Y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        echo "Skipping download. Models are ready for Docker build."
        exit 0
    fi
fi

# Check if Ollama is installed locally
if ! command -v ollama &> /dev/null; then
    echo "❌ Ollama is not installed on your local machine!"
    echo "   Please install Ollama first: https://ollama.ai/download"
    echo ""
    echo "   Or use Docker to download:"
    echo "   docker run -d --name ollama-temp -v \"\$(pwd)/ollama_models_local:/root/.ollama\" ollama/ollama:0.12.6"
    echo "   docker exec ollama-temp ollama pull llama3.2:1b"
    echo "   docker exec ollama-temp ollama pull nomic-embed-text"
    echo "   docker stop ollama-temp && docker rm ollama-temp"
    exit 1
fi

# Set OLLAMA_MODELS environment variable to use our local directory
export OLLAMA_MODELS="$(pwd)/ollama_models_local"

echo "📥 Pulling llama3.2..."
OLLAMA_MODELS="$(pwd)/ollama_models_local" ollama pull llama3.2
if [ $? -ne 0 ]; then
    echo "❌ Failed to download llama3.2"
    exit 1
fi

echo ""
echo "📥 Pulling nomic-embed-text..."
OLLAMA_MODELS="$(pwd)/ollama_models_local" ollama pull nomic-embed-text
if [ $? -ne 0 ]; then
    echo "❌ Failed to download nomic-embed-text"
    exit 1
fi

echo ""
echo "✅ Models downloaded successfully!"
echo "📁 Models are stored in: ./ollama_models_local"
echo ""
echo "You can now run: docker compose build"
