#!/bin/bash

echo "🔧 Model Configuration Test"
echo "==========================="
echo ""

# Source environment from docker-compose.yml values (for testing outside Docker)
export LLM_MODEL="${LLM_MODEL:-qwen2.5-vl:7b}"
export EMBEDDING_MODEL="${EMBEDDING_MODEL:-nomic-embed-text}"
export LLM_GGUF_FILE="${LLM_GGUF_FILE:-Qwen2.5-VL-7B-Instruct-Q4_K_M.gguf}"
export LLM_MMPROJ_FILE="${LLM_MMPROJ_FILE:-mmproj-Qwen2.5-VL-7B-Instruct-f16.gguf}"
export EMBEDDING_GGUF_FILE="${EMBEDDING_GGUF_FILE:-nomic-embed-text-v1.5.Q4_K_M.gguf}"

echo "📋 Current Configuration:"
echo "  LLM_MODEL: $LLM_MODEL"
echo "  EMBEDDING_MODEL: $EMBEDDING_MODEL"
echo "  LLM_GGUF_FILE: $LLM_GGUF_FILE"
echo "  LLM_MMPROJ_FILE: $LLM_MMPROJ_FILE"
echo "  EMBEDDING_GGUF_FILE: $EMBEDDING_GGUF_FILE"
echo ""

echo "📁 Checking gguf_models directory:"
if [ -d "./gguf_models" ]; then
    echo "  ✅ Directory exists"
    echo "  📦 Contents:"
    ls -la ./gguf_models/ | sed 's/^/     /'
    echo ""
    
    echo "🔍 Model file checks:"
    if [ -f "./gguf_models/$LLM_GGUF_FILE" ]; then
        echo "  ✅ LLM file found: $LLM_GGUF_FILE"
    else
        echo "  ❌ LLM file missing: $LLM_GGUF_FILE"
    fi
    
    if [ -f "./gguf_models/$LLM_MMPROJ_FILE" ]; then
        echo "  ✅ LLM mmproj file found: $LLM_MMPROJ_FILE"
    else
        echo "  ❌ LLM mmproj file missing: $LLM_MMPROJ_FILE"
    fi
    
    if [ -f "./gguf_models/$EMBEDDING_GGUF_FILE" ]; then
        echo "  ✅ Embedding file found: $EMBEDDING_GGUF_FILE"
    else
        echo "  ❌ Embedding file missing: $EMBEDDING_GGUF_FILE"
    fi
else
    echo "  ❌ Directory missing"
fi

echo ""
echo "🔧 Script compatibility test:"

echo "  📥 fetch-models.sh:"
cd "$(dirname "$0")"
source helper-scripts/fetch-models.sh > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "     ✅ Loads without Python dependencies"
    echo "     📋 Detected: LLM=$LLM_MODEL, Embedding=$EMBEDDING_MODEL"
else
    echo "     ❌ Failed to load"
fi

echo ""
echo "✅ Configuration test complete!"
echo ""
echo "💡 To change models, edit the environment section in docker-compose.yml"