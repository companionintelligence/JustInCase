#!/bin/bash

echo "🔧 Fixing Ollama Model Registration"
echo "==================================="
echo ""

# Check if ollama container is running
if ! docker ps | grep -q ollama; then
    echo "❌ Ollama container is not running!"
    echo "   Please run: docker compose up -d ollama"
    exit 1
fi

echo "📋 Current state (should be empty):"
docker exec ollama ollama list

echo ""
echo "🔍 Checking mounted directory in container:"
docker exec ollama ls -la /root/.ollama/models/manifests/registry.ollama.ai/library/ 2>/dev/null || echo "No library directory"

echo ""
echo "🔄 Attempting to trigger model registration..."

# Get model names from config
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

# Try to create symlinks to help Ollama find the models
echo ""
echo "📝 Creating model registry entries..."

# For each model, try to help Ollama recognize it
for model in "$LLM_MODEL" "$EMBEDDING_MODEL"; do
    echo "Processing $model..."
    
    # Check if the model exists in the mounted directory
    if docker exec ollama test -d "/root/.ollama/models/manifests/registry.ollama.ai/library/$model"; then
        echo "✅ Found $model in mounted directory"
        
        # Try to list the specific model to trigger registration
        docker exec ollama ollama show "$model" 2>/dev/null || echo "  Model not yet registered"
    else
        echo "❌ $model not found in mounted directory"
    fi
done

echo ""
echo "🔄 Restarting Ollama to force re-scan..."
docker compose restart ollama

echo ""
echo "⏳ Waiting for Ollama to restart..."
sleep 10

echo ""
echo "📋 Final model list:"
docker exec ollama ollama list

echo ""
echo "If models still aren't showing, try:"
echo "1. docker compose down"
echo "2. docker compose up -d ollama"
echo "3. docker exec ollama ollama pull llama3.2"
echo "4. docker exec ollama ollama pull nomic-embed-text"
