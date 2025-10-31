#!/bin/bash

echo "🚀 Model Download Guide (LOCAL ONLY)"
echo "===================================="
echo ""
echo "This system requires models to be downloaded LOCALLY before use."
echo "Docker will NOT download models from the internet."
echo ""

# Get model names and URLs from environment variables or use defaults
LLM_MODEL="${LLM_MODEL:-qwen2.5-vl:7b}"
EMBEDDING_MODEL="${EMBEDDING_MODEL:-nomic-embed-text}"

# Default GGUF file names and repositories
QWEN_GGUF_REPO="${QWEN_GGUF_REPO:-ggml-org/Qwen2.5-VL-7B-Instruct-GGUF}"
LLM_FILE="${LLM_GGUF_FILE:-Qwen2.5-VL-7B-Instruct-Q4_K_M.gguf}"
LLM_URL="https://huggingface.co/${QWEN_GGUF_REPO}/resolve/main/${LLM_FILE}"
MMPROJ_FILE="${LLM_MMPROJ_FILE:-mmproj-Qwen2.5-VL-7B-Instruct-f16.gguf}"
MMPROJ_URL="https://huggingface.co/${QWEN_GGUF_REPO}/resolve/main/${MMPROJ_FILE}"

NOMIC_GGUF_REPO="${NOMIC_GGUF_REPO:-nomic-ai/nomic-embed-text-v1.5-GGUF}"
NOMIC_FILE="${EMBEDDING_GGUF_FILE:-nomic-embed-text-v1.5.Q4_K_M.gguf}"
NOMIC_URL="https://huggingface.co/${NOMIC_GGUF_REPO}/resolve/main/${NOMIC_FILE}"

echo "📋 Required models:"
echo "  - LLM: $LLM_MODEL"
echo "  - Embeddings: $EMBEDDING_MODEL"
echo ""

echo "STEP 1: Download GGUF files manually"
echo "===================================="
echo "Download these files:"
echo ""
echo "1. Qwen2.5-VL 7B GGUF:"
echo "   URL: $LLM_URL"
echo "   File: $LLM_FILE"
echo ""
echo "2. Qwen2.5-VL Vision Projector:"
echo "   URL: $MMPROJ_URL"
echo "   File: $MMPROJ_FILE"
echo ""
echo "3. Nomic Embed GGUF:"
echo "   URL: $NOMIC_URL"
echo "   File: $NOMIC_FILE"
echo ""
echo "� Starting automatic download..."
echo ""

# Create directory
mkdir -p gguf_models

# Check if wget is available
if ! command -v wget >/dev/null 2>&1; then
    echo "❌ wget not found. Please install wget first:"
    echo "   brew install wget  # macOS"
    echo "   apt install wget   # Ubuntu/Debian"
    echo ""
    echo "Or download manually:"
    echo "  curl -L -o \"gguf_models/$LLM_FILE\" \"$LLM_URL\""
    echo "  curl -L -o \"gguf_models/$NOMIC_FILE\" \"$NOMIC_URL\""
    exit 1
fi

# Download LLM file
if [ -f "./gguf_models/$LLM_FILE" ]; then
    echo "✅ LLM file already exists: $LLM_FILE"
else
    echo "📥 Downloading LLM model: $LLM_FILE"
    echo "   From: $LLM_URL"
    echo "   Size: ~4.6GB (this may take a while...)"
    echo ""
    
    wget -P gguf_models/ --progress=bar:force:noscroll "$LLM_URL"
    if [ $? -eq 0 ]; then
        echo ""
        echo "✅ Successfully downloaded $LLM_FILE"
    else
        echo ""
        echo "❌ Failed to download $LLM_FILE"
        echo "💡 Check your internet connection and try again"
        exit 1
    fi
fi

echo ""

# Download mmproj file
if [ -f "./gguf_models/$MMPROJ_FILE" ]; then
    echo "✅ Vision projector file already exists: $MMPROJ_FILE"
else
    echo "📥 Downloading vision projector: $MMPROJ_FILE"
    echo "   From: $MMPROJ_URL"
    echo "   Size: ~1.3GB"
    echo ""
    
    wget -P gguf_models/ --progress=bar:force:noscroll "$MMPROJ_URL"
    if [ $? -eq 0 ]; then
        echo ""
        echo "✅ Successfully downloaded $MMPROJ_FILE"
    else
        echo ""
        echo "❌ Failed to download $MMPROJ_FILE"
        exit 1
    fi
fi

echo ""

# Download embedding file
if [ -f "./gguf_models/$NOMIC_FILE" ]; then
    echo "✅ Embedding file already exists: $NOMIC_FILE"
else
    echo "📥 Downloading embedding model: $NOMIC_FILE"
    echo "   From: $NOMIC_URL"
    echo "   Size: ~80MB"
    echo ""
    
    wget -P gguf_models/ --progress=bar:force:noscroll "$NOMIC_URL"
    if [ $? -eq 0 ]; then
        echo ""
        echo "✅ Successfully downloaded $NOMIC_FILE"
    else
        echo ""
        echo "❌ Failed to download $NOMIC_FILE"
        exit 1
    fi
fi

echo ""

echo "STEP 2: Verify downloads"
echo "========================"
if [ -f "./gguf_models/$LLM_FILE" ] && [ -f "./gguf_models/$MMPROJ_FILE" ] && [ -f "./gguf_models/$NOMIC_FILE" ]; then
    echo "✅ All required files downloaded successfully!"
    echo ""
    echo "📦 Downloaded files:"
    ls -lh ./gguf_models/$LLM_FILE ./gguf_models/$MMPROJ_FILE ./gguf_models/$NOMIC_FILE
    echo ""
    echo "🚀 Next steps:"
    echo "   docker compose up --build"
    echo ""
    echo "💡 The C++ server will load the GGUF models directly from ./gguf_models/"
    echo "💡 Vision models need both the main model and mmproj (vision projector) files"
else
    echo "❌ Some files are missing. Please check the download."
fi

echo ""
echo "STEP 3: Run the system"
echo "======================"
echo "Start the system:"
echo "  docker compose up --build"
echo ""
echo "Access the web interface:"
echo "  http://localhost:8080"

# Check if gguf_models directory exists
if [ -d "./gguf_models" ]; then
    echo ""
    echo "📁 Found gguf_models directory. Contents:"
    ls -la ./gguf_models/*.gguf 2>/dev/null || echo "  No .gguf files found yet"
else
    echo ""
    echo "📁 No gguf_models directory found. Create it with: mkdir -p gguf_models"
fi
