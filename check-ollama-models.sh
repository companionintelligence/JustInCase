#!/bin/bash

echo "🔍 Checking Ollama Models Directory"
echo "==================================="
echo ""

if [ ! -d "./ollama_models" ]; then
    echo "❌ ./ollama_models directory not found!"
    exit 1
fi

echo "📁 Directory structure:"
echo "All directories:"
find ./ollama_models -type d | head -20
echo ""
echo "Directories with model names:"
find ./ollama_models -type d -name "*llama*" -o -name "*nomic*" | sort

echo ""
echo "📄 Manifest files:"
find ./ollama_models -name "*" -path "*/manifests/*" -type f | sort

echo ""
echo "🗄️ Blob files:"
find ./ollama_models -type f -path "*/blobs/*" | wc -l
echo "Total blob files found"

echo ""
echo "💾 Total size:"
du -sh ./ollama_models

echo ""
echo "📝 Permissions:"
ls -la ./ollama_models/

if docker ps | grep -q ollama; then
    echo ""
    echo "🐳 Models visible in running container:"
    docker exec ollama ollama list
    
    echo ""
    echo "🔍 Checking container's view of models:"
    docker exec ollama ls -la /root/.ollama/ 2>/dev/null || echo "Cannot access container"
    docker exec ollama ls -la /root/.ollama/models/ 2>/dev/null || echo "No models directory in container"
fi
