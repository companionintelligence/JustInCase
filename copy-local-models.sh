#!/bin/bash

echo "🔄 Copy Local Ollama Models to Docker"
echo "====================================="
echo "This copies models from your local Ollama to Docker volume"
echo ""

# Check if local Ollama directory exists
if [ ! -d "$HOME/.ollama/models" ]; then
    echo "❌ No local Ollama models found at ~/.ollama/models"
    exit 1
fi

echo "📋 Local models found:"
ls -la ~/.ollama/models/manifests/registry.ollama.ai/library/ 2>/dev/null || echo "No models found"

echo ""
echo "🐳 Starting temporary container to copy models..."

# Create a temporary container with both volumes mounted
docker run -d --name ollama-copy \
    -v "$HOME/.ollama:/local-ollama:ro" \
    -v "ollama_data:/root/.ollama" \
    ollama/ollama:0.12.6 \
    sleep 3600

# Wait for container to start
sleep 2

echo ""
echo "📦 Copying models..."
# Copy the entire models structure
docker exec ollama-copy sh -c "cp -r /local-ollama/* /root/.ollama/ 2>/dev/null || true"

echo ""
echo "🧹 Cleaning up..."
docker stop ollama-copy
docker rm ollama-copy

echo ""
echo "✅ Done! Your local models have been copied to Docker volume"
echo ""
echo "Run ./check-models.sh to verify"
