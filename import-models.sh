#!/bin/bash

echo "🔄 Import Models to Docker Ollama"
echo "================================="
echo ""

# Get model names from config
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

echo "📋 Models to import:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

# Start ollama if not running
if ! docker ps | grep -q ollama; then
    echo "Starting Ollama container..."
    docker compose up -d ollama
    echo "Waiting for Ollama to be ready..."
    sleep 5
fi

# Check current models
echo "📋 Current models in Docker:"
docker exec ollama ollama list

# For each model, check if it needs to be pulled
for model in "$LLM_MODEL" "$EMBEDDING_MODEL"; do
    echo ""
    echo "Checking $model..."
    if docker exec ollama ollama list | grep -q "^$model"; then
        echo "✅ $model already available"
    else
        echo "📥 Pulling $model..."
        docker exec ollama ollama pull "$model"
        if [ $? -eq 0 ]; then
            echo "✅ Successfully pulled $model"
        else
            echo "❌ Failed to pull $model"
            exit 1
        fi
    fi
done

echo ""
echo "✅ All models are now available in Docker!"
echo ""
echo "📋 Final model list:"
docker exec ollama ollama list
