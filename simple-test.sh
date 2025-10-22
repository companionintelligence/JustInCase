#!/bin/bash

echo "🧪 Simple Direct Model Loading Test"
echo "==================================="
echo ""

# Start just Ollama
echo "1. Starting Ollama container..."
docker compose up -d ollama
sleep 5

# Check what's mounted
echo ""
echo "2. Checking mounted GGUF files in container..."
docker exec ollama ls -la /gguf_models/

# Try to create models directly
echo ""
echo "3. Creating llama3.2 directly from GGUF..."
docker exec ollama ollama create llama3.2 -f - <<EOF
FROM /gguf_models/Llama-3.2-1B-Instruct-Q4_K_S.gguf
EOF

echo ""
echo "4. Creating nomic-embed-text directly from GGUF..."
docker exec ollama ollama create nomic-embed-text -f - <<EOF
FROM /gguf_models/nomic-embed-text-v1.5.Q4_K_M.gguf
EOF

echo ""
echo "5. Listing models..."
docker exec ollama ollama list

echo ""
echo "6. Testing if models work..."
docker exec ollama ollama run llama3.2 "Say hello"
