#!/bin/bash

# Simple script to ensure models are available when Ollama starts
# This runs INSIDE the main docker compose, not as a separate container

echo "🚀 Model Setup"
echo "============="

# Check if we should skip model operations
if [ "$SKIP_MODEL_PULL" = "true" ]; then
    echo "SKIP_MODEL_PULL is set, skipping model downloads"
    exit 0
fi

# Wait for Ollama to be ready
echo "Waiting for Ollama to be ready..."
for i in {1..30}; do
    if curl -s http://ollama:11434/api/tags >/dev/null 2>&1; then
        echo "Ollama is ready!"
        break
    fi
    sleep 2
done

# Function to check and pull models
ensure_model() {
    local model=$1
    echo ""
    echo "Checking $model..."
    
    # Check if model exists
    if docker exec ollama ollama list 2>/dev/null | grep -q "$model"; then
        echo "✅ $model already available"
        return 0
    fi
    
    echo "📥 Pulling $model (this may take a while on first run)..."
    docker exec ollama ollama pull $model
    if [ $? -eq 0 ]; then
        echo "✅ Successfully pulled $model"
    else
        echo "❌ Failed to pull $model"
        return 1
    fi
}

# Ensure required models are available
ensure_model "llama3.2:1b"
ensure_model "nomic-embed-text"

echo ""
echo "✅ Model setup complete!"
