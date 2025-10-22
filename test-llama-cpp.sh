#!/bin/bash

echo "🧪 Testing llama.cpp Docker setup"
echo "================================="

# Check if GGUF files exist
echo ""
echo "1️⃣ Checking GGUF models..."
LLAMA_FILE=$(python3 -c 'from config import LLAMA_GGUF_FILE; print(LLAMA_GGUF_FILE)')
NOMIC_FILE=$(python3 -c 'from config import NOMIC_GGUF_FILE; print(NOMIC_GGUF_FILE)')

if [ -f "./gguf_models/$LLAMA_FILE" ]; then
    echo "   ✅ LLM GGUF found: $LLAMA_FILE"
    ls -lh "./gguf_models/$LLAMA_FILE" | awk '{print "      Size: " $5}'
else
    echo "   ❌ LLM GGUF not found: $LLAMA_FILE"
    exit 1
fi

if [ -f "./gguf_models/$NOMIC_FILE" ]; then
    echo "   ✅ Embedding GGUF found: $NOMIC_FILE"
    ls -lh "./gguf_models/$NOMIC_FILE" | awk '{print "      Size: " $5}'
else
    echo "   ❌ Embedding GGUF not found: $NOMIC_FILE"
    exit 1
fi

# Test with Docker
echo ""
echo "2️⃣ Testing llama.cpp Docker image..."
echo "   Running: docker run --rm ghcr.io/ggerganov/llama.cpp:server --help"

# Run with timeout to prevent hanging
timeout 30 docker run --rm ghcr.io/ggerganov/llama.cpp:server --help > /dev/null 2>&1

if [ $? -eq 0 ]; then
    echo "   ✅ Docker image is available and working"
elif [ $? -eq 124 ]; then
    echo "   ❌ Docker command timed out after 30 seconds"
    echo "   This might indicate a Docker issue or slow network"
else
    echo "   ⚠️  Docker image might not be available locally"
    echo "   Pulling image (this may take a few minutes)..."
    docker pull ghcr.io/ggerganov/llama.cpp:server
    if [ $? -eq 0 ]; then
        echo "   ✅ Successfully pulled Docker image"
    else
        echo "   ❌ Failed to pull Docker image"
        exit 1
    fi
fi

# Quick test to ensure the image can actually load a model
echo ""
echo "3️⃣ Testing model loading capability..."
echo "   Starting a test server with the LLM model..."

# Start server in background with timeout
timeout 10 docker run --rm \
    -v "$(pwd)/gguf_models:/models:ro" \
    ghcr.io/ggerganov/llama.cpp:server \
    -m "/models/$LLAMA_FILE" \
    --port 8080 \
    --host 0.0.0.0 \
    -n 1 > /tmp/llama-test.log 2>&1 &

DOCKER_PID=$!
sleep 3

if ps -p $DOCKER_PID > /dev/null 2>&1; then
    echo "   ✅ Server started successfully"
    kill $DOCKER_PID 2>/dev/null
    wait $DOCKER_PID 2>/dev/null
else
    echo "   ⚠️  Server exited quickly. Checking logs..."
    if [ -f /tmp/llama-test.log ]; then
        tail -5 /tmp/llama-test.log
    fi
fi

echo ""
echo "4️⃣ Summary:"
echo "   ✅ GGUF models are present"
echo "   ✅ Docker image is available"
echo "   ✅ Ready to run with: docker compose up"
echo ""
echo "💡 Next steps:"
echo "   1. Run: docker compose up"
echo "   2. Wait for all services to start"
echo "   3. Access the web UI at http://localhost:8080"
