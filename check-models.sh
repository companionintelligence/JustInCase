#!/bin/bash

echo "🔍 Checking Ollama models directory structure"
echo "==========================================="

if [ -d "./ollama_models" ]; then
    echo "📁 Contents of ./ollama_models:"
    find ./ollama_models -type f -name "*.bin" -o -name "*.json" | head -20
    echo ""
    echo "📊 Directory structure:"
    tree -L 3 ./ollama_models 2>/dev/null || ls -la ./ollama_models/
    echo ""
    echo "💾 Total size:"
    du -sh ./ollama_models/
else
    echo "❌ ./ollama_models directory not found!"
fi

echo ""
echo "🐳 Checking models in running Ollama container:"
if docker ps | grep -q ollama; then
    docker exec ollama ollama list
else
    echo "❌ Ollama container not running"
fi
