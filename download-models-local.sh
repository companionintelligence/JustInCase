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

# Source the Python config to get model names
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

# Check if Ollama is installed locally
if ! command -v ollama &> /dev/null; then
    echo "❌ Ollama is not installed on your local machine!"
    echo ""
    echo "Using Docker to download models instead..."
    
    # Use Docker to download models
    echo "Starting temporary Ollama container..."
    docker run -d --name ollama-temp -v "$(pwd)/ollama_models_local:/root/.ollama" ollama/ollama:0.12.6
    
    # Wait for Ollama to start
    echo "Waiting for Ollama to start..."
    sleep 5
    
    echo ""
    echo "📥 Pulling $LLM_MODEL..."
    docker exec ollama-temp ollama pull "$LLM_MODEL"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to download $LLM_MODEL"
        docker stop ollama-temp && docker rm ollama-temp
        exit 1
    fi
    
    echo ""
    echo "📥 Pulling $EMBEDDING_MODEL..."
    docker exec ollama-temp ollama pull "$EMBEDDING_MODEL"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to download $EMBEDDING_MODEL"
        docker stop ollama-temp && docker rm ollama-temp
        exit 1
    fi
    
    echo ""
    echo "Cleaning up temporary container..."
    docker stop ollama-temp && docker rm ollama-temp
else
    echo "✅ Ollama is installed locally"
    echo ""
    echo "⚠️  Note: Your local Ollama might store models in a different location."
    echo "   We'll use Docker to ensure models are in the right place."
    echo ""
    
    # Use Docker anyway to ensure models are in the right location
    echo "Starting temporary Ollama container..."
    docker run -d --name ollama-temp -v "$(pwd)/ollama_models_local:/root/.ollama" ollama/ollama:0.12.6
    
    # Wait for Ollama to start
    echo "Waiting for Ollama to start..."
    sleep 5
    
    echo ""
    echo "📥 Pulling $LLM_MODEL..."
    docker exec ollama-temp ollama pull "$LLM_MODEL"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to download $LLM_MODEL"
        docker stop ollama-temp && docker rm ollama-temp
        exit 1
    fi
    
    echo ""
    echo "📥 Pulling $EMBEDDING_MODEL..."
    docker exec ollama-temp ollama pull "$EMBEDDING_MODEL"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to download $EMBEDDING_MODEL"
        docker stop ollama-temp && docker rm ollama-temp
        exit 1
    fi
    
    echo ""
    echo "Cleaning up temporary container..."
    docker stop ollama-temp && docker rm ollama-temp
fi

echo ""
echo "✅ Models downloaded successfully!"
echo "📁 Models are stored in: ./ollama_models_local"
echo ""
echo "📊 Verifying downloaded models:"
ls -la ./ollama_models_local/
echo ""
echo "You can now run: docker compose build"
