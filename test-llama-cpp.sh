#!/bin/bash

echo "🧪 Testing llama.cpp setup"
echo "========================="

# Check if GGUF files exist
echo "Checking GGUF models..."
if [ -f "./gguf_models/$(python3 -c 'from config import LLAMA_GGUF_FILE; print(LLAMA_GGUF_FILE)')" ]; then
    echo "✅ LLM GGUF found"
else
    echo "❌ LLM GGUF not found"
    exit 1
fi

if [ -f "./gguf_models/$(python3 -c 'from config import NOMIC_GGUF_FILE; print(NOMIC_GGUF_FILE)')" ]; then
    echo "✅ Embedding GGUF found"
else
    echo "❌ Embedding GGUF not found"
    exit 1
fi

# Build llama.cpp if needed
if [ ! -f "./llama.cpp/build/bin/llama-server" ]; then
    echo "Building llama.cpp..."
    chmod +x build-llama-cpp.sh
    if ! ./build-llama-cpp.sh; then
        echo "❌ Failed to build llama.cpp"
        exit 1
    fi
fi

# Verify the binary exists
if [ ! -f "./llama.cpp/build/bin/llama-server" ]; then
    echo "❌ llama-server binary not found after build!"
    echo "Contents of llama.cpp/build/:"
    ls -la ./llama.cpp/build/ 2>/dev/null || echo "Build directory not found"
    echo "Contents of llama.cpp/build/bin/:"
    ls -la ./llama.cpp/build/bin/ 2>/dev/null || echo "Bin directory not found"
    exit 1
fi

# Test the server directly
echo ""
echo "Testing llama.cpp server directly..."
./llama.cpp/build/bin/llama-server \
    -m "./gguf_models/$(python3 -c 'from config import LLAMA_GGUF_FILE; print(LLAMA_GGUF_FILE)')" \
    --port 8083 \
    --host 127.0.0.1 \
    -c 512 &

SERVER_PID=$!
sleep 5

# Test completion
echo "Testing completion..."
response=$(curl -s http://localhost:8083/completion \
    -H "Content-Type: application/json" \
    -d '{"prompt": "Hello, how are you?", "n_predict": 50}')

if [ $? -eq 0 ]; then
    echo "✅ Server responded successfully"
    if command -v jq &> /dev/null; then
        echo "$response" | jq .
    else
        echo "$response"
    fi
else
    echo "❌ Server failed to respond"
fi

# Cleanup
kill $SERVER_PID 2>/dev/null

echo ""
echo "✅ llama.cpp test complete!"
