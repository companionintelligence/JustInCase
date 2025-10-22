#!/bin/bash

echo "🔍 Checking Ollama models"
echo "========================"

echo ""
echo "🐳 Models in Ollama container:"
if docker ps | grep -q ollama; then
    docker exec ollama ollama list
else
    echo "❌ Ollama container not running"
    echo "   Run: docker compose up -d ollama"
fi

echo ""
echo "📊 Docker volume info:"
docker volume inspect ollama_data 2>/dev/null || echo "Volume not created yet"
