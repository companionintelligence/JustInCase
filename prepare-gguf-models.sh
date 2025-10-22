#!/bin/bash

echo "🚀 Prepare GGUF Models for Ollama"
echo "================================="
echo ""
echo "This script helps you use pre-downloaded GGUF model files"
echo ""

# Create models directory
mkdir -p ./gguf_models

# Get model names from config
LLM_MODEL=$(python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

echo "📋 Required models:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

# Check if GGUF files exist
echo "🔍 Checking for GGUF files in ./gguf_models/..."
echo ""

if [ ! -d "./gguf_models" ]; then
    echo "❌ No gguf_models directory found!"
    echo ""
    echo "📥 To download GGUF files manually:"
    echo ""
    echo "1. For Llama 3.2 (1B or 3B):"
    echo "   Visit: https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF"
    echo "   Or: https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF"
    echo "   Download a quantized version (e.g., Q4_K_M.gguf)"
    echo ""
    echo "2. For nomic-embed-text:"
    echo "   Visit: https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF"
    echo "   Download: nomic-embed-text-v1.5.Q4_K_M.gguf"
    echo ""
    echo "3. Place the .gguf files in ./gguf_models/"
    echo ""
    exit 1
fi

# List available GGUF files
echo "📦 Available GGUF files:"
ls -la ./gguf_models/*.gguf 2>/dev/null || echo "No .gguf files found"
echo ""

# Create Modelfiles for each model
echo "📝 Creating Modelfiles..."

# Create Modelfile for LLM
if ls ./gguf_models/*llama*.gguf 1> /dev/null 2>&1; then
    LLAMA_GGUF=$(ls ./gguf_models/*llama*.gguf | head -1)
    echo "Found Llama GGUF: $LLAMA_GGUF"
    
    cat > ./gguf_models/Modelfile.llama <<EOF
FROM $LLAMA_GGUF

# Set parameters for chat
PARAMETER temperature 0.7
PARAMETER top_p 0.9
PARAMETER stop "<|start_header_id|>"
PARAMETER stop "<|end_header_id|>"
PARAMETER stop "<|eot_id|>"

# Chat template
TEMPLATE """{{ if .System }}<|start_header_id|>system<|end_header_id|>

{{ .System }}<|eot_id|>{{ end }}{{ if .Prompt }}<|start_header_id|>user<|end_header_id|>

{{ .Prompt }}<|eot_id|>{{ end }}<|start_header_id|>assistant<|end_header_id|>

{{ .Response }}<|eot_id|>"""

# System prompt
SYSTEM """You are a helpful emergency preparedness assistant. Provide clear, practical advice based on the provided context."""
EOF
    echo "✅ Created Modelfile.llama"
else
    echo "⚠️  No Llama GGUF file found"
fi

# Create Modelfile for embeddings
if ls ./gguf_models/*nomic*.gguf 1> /dev/null 2>&1; then
    NOMIC_GGUF=$(ls ./gguf_models/*nomic*.gguf | head -1)
    echo "Found Nomic GGUF: $NOMIC_GGUF"
    
    cat > ./gguf_models/Modelfile.nomic <<EOF
FROM $NOMIC_GGUF
EOF
    echo "✅ Created Modelfile.nomic"
else
    echo "⚠️  No Nomic GGUF file found"
fi

echo ""
echo "✅ Modelfiles created in ./gguf_models/"
echo ""
echo "Next steps:"
echo "1. Start Docker: docker compose up -d"
echo "2. Load models: ./load-gguf-models.sh"
