#!/bin/bash

echo "🔍 Checking Ollama Models Directory"
echo "==================================="
echo ""

if [ ! -d "./ollama_models" ]; then
    echo "❌ ./ollama_models directory not found!"
    exit 1
fi

echo "📁 Directory structure:"
find ./ollama_models -type d -name "llama*" -o -name "nomic*" | sort

echo ""
echo "📄 Manifest files:"
find ./ollama_models -name "*.json" -path "*/manifests/*" | grep -E "(llama|nomic)" | sort

echo ""
echo "🗄️ Blob files:"
find ./ollama_models -type f -path "*/blobs/*" | wc -l
echo "Total blob files found"

echo ""
echo "💾 Total size:"
du -sh ./ollama_models

if docker ps | grep -q ollama; then
    echo ""
    echo "🐳 Models visible in running container:"
    docker exec ollama ollama list
fi
