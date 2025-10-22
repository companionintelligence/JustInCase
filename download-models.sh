#!/bin/bash

# This script downloads Ollama models directly to your host machine
# Run this ONCE to avoid re-downloading during development

echo "🚀 Ollama Model Downloader"
echo "========================="
echo "This will download models to ./ollama_models on your host machine"
echo "These models will persist across Docker rebuilds!"
echo ""

# Create the models directory
mkdir -p ollama_models

# Check if models already exist
if [ -d "ollama_models/models" ] && [ "$(ls -A ollama_models/models 2>/dev/null)" ]; then
    echo "⚠️  Models directory already exists and contains files."
    echo "   Contents:"
    ls -la ollama_models/models/
    read -p "Do you want to continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 0
    fi
fi

# Start a temporary Ollama container with our local directory mounted
echo ""
echo "Starting temporary Ollama container..."
docker run -d --name ollama-temp \
    -v "$(pwd)/ollama_models:/root/.ollama" \
    ollama/ollama:0.12.6

# Wait for Ollama to be ready
echo "Waiting for Ollama to start..."
sleep 5

# Function to pull a model
pull_model() {
    local model=$1
    echo ""
    echo "📥 Pulling $model..."
    docker exec ollama-temp ollama pull $model
    if [ $? -eq 0 ]; then
        echo "✅ Successfully downloaded $model"
    else
        echo "❌ Failed to download $model"
        return 1
    fi
}

# Pull the required models
pull_model "llama3.2:1b"
pull_model "nomic-embed-text"

# List downloaded models
echo ""
echo "📋 Downloaded models:"
docker exec ollama-temp ollama list

# Clean up
echo ""
echo "Cleaning up temporary container..."
docker stop ollama-temp
docker rm ollama-temp

echo ""
echo "✨ Done! Models are stored in ./ollama_models"
echo "   These will be automatically used by docker compose"
echo ""
echo "To use: docker compose up --build"
