#!/bin/bash

echo "🚀 Manual Model Puller"
echo "===================="
echo "This script pulls models directly into the running Ollama container"
echo ""

# Check if Ollama is running
if ! docker ps | grep -q ollama; then
    echo "❌ Ollama container is not running!"
    echo "   Please run: docker compose up -d ollama"
    exit 1
fi

echo "📋 Current models:"
docker exec ollama ollama list

echo ""
echo "📥 Pulling llama3.2:1b..."
docker exec ollama ollama pull llama3.2:1b

echo ""
echo "📥 Pulling nomic-embed-text..."
docker exec ollama ollama pull nomic-embed-text

echo ""
echo "✅ Done! Current models:"
docker exec ollama ollama list

echo ""
echo "You can now run: docker compose up --build"
