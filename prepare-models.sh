#!/bin/bash

echo "🚀 Prepare Models for Docker"
echo "============================"
echo ""

# Get configuration
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")
USE_LOCAL_OLLAMA=$(python3 -c "from config import USE_LOCAL_OLLAMA; print(USE_LOCAL_OLLAMA)" 2>/dev/null || echo "false")

echo "📋 Required models:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

# Create target directory
mkdir -p ollama_models

# Check if we should use local Ollama
if [ "$USE_LOCAL_OLLAMA" = "True" ] || [ "$USE_LOCAL_OLLAMA" = "true" ]; then
    echo "🏠 Using local Ollama installation (USE_LOCAL_OLLAMA=true)"
    
    # Check if local Ollama is installed
    if ! command -v ollama &> /dev/null; then
        echo "❌ Ollama is not installed locally!"
        echo "   Please install Ollama first: https://ollama.ai/download"
        echo "   Or set USE_LOCAL_OLLAMA=false to fetch models directly"
        exit 1
    fi
    
    # Check if local Ollama directory exists
    if [ ! -d "$HOME/.ollama" ]; then
        echo "⚠️  No local Ollama directory found at ~/.ollama"
        echo "   Will pull models locally first..."
        mkdir -p "$HOME/.ollama"
    fi
    
    # Check and pull missing models locally
    echo "🔍 Checking local models..."
    NEED_PULL=false
    
    # Check for LLM model
    if ollama list 2>/dev/null | grep -q "^$LLM_MODEL"; then
        echo "✅ $LLM_MODEL already available locally"
    else
        echo "📥 $LLM_MODEL not found locally, pulling..."
        NEED_PULL=true
        ollama pull "$LLM_MODEL"
        if [ $? -ne 0 ]; then
            echo "❌ Failed to pull $LLM_MODEL"
            exit 1
        fi
    fi
    
    # Check for embedding model
    if ollama list 2>/dev/null | grep -q "^$EMBEDDING_MODEL"; then
        echo "✅ $EMBEDDING_MODEL already available locally"
    else
        echo "📥 $EMBEDDING_MODEL not found locally, pulling..."
        NEED_PULL=true
        ollama pull "$EMBEDDING_MODEL"
        if [ $? -ne 0 ]; then
            echo "❌ Failed to pull $EMBEDDING_MODEL"
            exit 1
        fi
    fi
    
    if [ "$NEED_PULL" = true ]; then
        echo ""
        echo "⏳ Waiting for models to be fully registered..."
        sleep 3
    fi
    
    # Copy from local Ollama
    echo ""
    echo "📦 Copying models from ~/.ollama to ./ollama_models..."
    if [ -d "$HOME/.ollama/models" ]; then
        echo "  Copying model files (this may take a moment)..."
        if command -v rsync &> /dev/null; then
            rsync -av --progress "$HOME/.ollama/models" ./ollama_models/
        else
            cp -r "$HOME/.ollama/models" ./ollama_models/
        fi
    else
        echo "❌ No models directory found in ~/.ollama"
        exit 1
    fi
else
    echo "🐳 Fetching models directly (USE_LOCAL_OLLAMA=false)"
    echo "   This will download models into ./ollama_models"
    echo ""
    
    # Use Docker to fetch models
    echo "Starting temporary Ollama container..."
    docker run -d --name ollama-temp \
        -v "$(pwd)/ollama_models:/root/.ollama" \
        ollama/ollama:0.12.6
    
    # Wait for Ollama to start
    echo "Waiting for Ollama to start..."
    for i in {1..30}; do
        if docker exec ollama-temp ollama list >/dev/null 2>&1; then
            break
        fi
        sleep 2
    done
    
    # Check existing models
    echo ""
    echo "📋 Checking existing models..."
    docker exec ollama-temp ollama list
    
    # Pull LLM model
    echo ""
    echo "📥 Pulling $LLM_MODEL..."
    docker exec ollama-temp ollama pull "$LLM_MODEL"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to pull $LLM_MODEL"
        docker stop ollama-temp && docker rm ollama-temp
        exit 1
    fi
    
    # Pull embedding model
    echo ""
    echo "📥 Pulling $EMBEDDING_MODEL..."
    docker exec ollama-temp ollama pull "$EMBEDDING_MODEL"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to pull $EMBEDDING_MODEL"
        docker stop ollama-temp && docker rm ollama-temp
        exit 1
    fi
    
    # Clean up
    echo ""
    echo "🧹 Cleaning up temporary container..."
    docker stop ollama-temp && docker rm ollama-temp
fi

# Verify models were copied
echo ""
echo "🔍 Verifying copied models..."
MISSING_MODELS=()

# Show what's in the models directory
echo ""
echo "📁 Model directory structure:"
if [ -d "./ollama_models/models" ]; then
    echo "  manifests:"
    ls -la ./ollama_models/models/manifests/registry.ollama.ai/library/ 2>/dev/null | head -10
    echo "  blobs:"
    ls -la ./ollama_models/models/blobs/ 2>/dev/null | head -5
fi

# Check for LLM model - handle both with and without tag
LLM_BASE=$(echo "$LLM_MODEL" | cut -d: -f1)
if [ -d "./ollama_models/models/manifests/registry.ollama.ai/library/$LLM_MODEL" ] || \
   [ -d "./ollama_models/models/manifests/registry.ollama.ai/library/$LLM_BASE" ]; then
    echo "✅ $LLM_MODEL copied successfully"
else
    echo "❌ $LLM_MODEL not found after copy"
    MISSING_MODELS+=("$LLM_MODEL")
fi

# Check for embedding model
EMBED_BASE=$(echo "$EMBEDDING_MODEL" | cut -d: -f1)
if [ -d "./ollama_models/models/manifests/registry.ollama.ai/library/$EMBEDDING_MODEL" ] || \
   [ -d "./ollama_models/models/manifests/registry.ollama.ai/library/$EMBED_BASE" ]; then
    echo "✅ $EMBEDDING_MODEL copied successfully"
else
    echo "❌ $EMBEDDING_MODEL not found after copy"
    MISSING_MODELS+=("$EMBEDDING_MODEL")
fi

if [ ${#MISSING_MODELS[@]} -gt 0 ]; then
    echo ""
    echo "❌ Failed to copy models: ${MISSING_MODELS[*]}"
    echo "   This might be a permission issue or the models weren't fully downloaded."
    exit 1
fi

echo ""
echo "✅ All required models are available!"
echo "📁 Models prepared in: ./ollama_models"
echo "📊 Total size: $(du -sh ./ollama_models 2>/dev/null | cut -f1 || echo 'Unknown')"

# Ensure proper permissions for Docker
echo ""
echo "🔧 Setting permissions for Docker access..."
chmod -R 755 ./ollama_models 2>/dev/null || true

echo ""
echo "You can now run: docker compose up --build"
