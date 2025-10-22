#!/bin/bash

echo "📥 Loading GGUF Models into Ollama"
echo "=================================="
echo ""

# Check if Ollama is running
if ! docker ps | grep -q ollama; then
    echo "❌ Ollama container is not running!"
    echo "   Please run: docker compose up -d ollama"
    exit 1
fi

# Get model names from config
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

echo "📋 Loading models:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

# Check current models
echo "📊 Current models in Ollama:"
docker exec ollama ollama list
echo ""

# Load LLM model
if [ -f "./gguf_models/Modelfile.llama" ]; then
    echo "🔄 Creating $LLM_MODEL from GGUF..."
    
    # Try creating from within the gguf_models directory
    docker exec ollama sh -c "cd /gguf_models && ollama create $LLM_MODEL -f Modelfile.llama"
    
    if [ $? -eq 0 ]; then
        echo "✅ Successfully created $LLM_MODEL"
    else
        echo "❌ Failed to create $LLM_MODEL"
        echo ""
        echo "Debugging information:"
        echo "1. Container's view of /gguf_models:"
        docker exec ollama ls -la /gguf_models/
        echo ""
        echo "2. Modelfile.llama contents:"
        docker exec ollama cat /gguf_models/Modelfile.llama
        echo ""
        echo "3. Checking if GGUF file exists:"
        docker exec ollama sh -c "cd /gguf_models && ls -la *.gguf"
    fi
else
    echo "⚠️  No Modelfile.llama found. Run ./prepare-gguf-models.sh first"
fi

echo ""

# Load embedding model
if [ -f "./gguf_models/Modelfile.nomic" ]; then
    echo "🔄 Creating $EMBEDDING_MODEL from GGUF..."
    
    # Try creating from within the gguf_models directory
    docker exec ollama sh -c "cd /gguf_models && ollama create $EMBEDDING_MODEL -f Modelfile.nomic"
    
    if [ $? -eq 0 ]; then
        echo "✅ Successfully created $EMBEDDING_MODEL"
    else
        echo "❌ Failed to create $EMBEDDING_MODEL"
        echo ""
        echo "Debugging information:"
        echo "1. Container's view of /gguf_models:"
        docker exec ollama ls -la /gguf_models/
        echo ""
        echo "2. Modelfile.nomic contents:"
        docker exec ollama cat /gguf_models/Modelfile.nomic
        echo ""
        echo "3. Checking if GGUF file exists:"
        docker exec ollama sh -c "cd /gguf_models && ls -la *.gguf"
    fi
else
    echo "⚠️  No Modelfile.nomic found. Run ./prepare-gguf-models.sh first"
fi

echo ""
echo "📊 Final model list:"
docker exec ollama ollama list
echo ""
echo "✅ Models loaded! You can now run: docker compose up"
