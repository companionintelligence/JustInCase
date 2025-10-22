#!/bin/bash

echo "🧪 Simple Direct Model Loading Test (NO NETWORK)"
echo "==============================================="
echo ""

# Start just Ollama
echo "1. Starting Ollama container..."
docker compose up -d ollama
sleep 5

# Check what's mounted
echo ""
echo "2. Checking mounted GGUF files in container..."
docker exec ollama ls -la /gguf_models/

# Check if GGUF files exist
echo ""
echo "3. Verifying GGUF files are accessible..."
docker exec ollama test -f /gguf_models/Llama-3.2-1B-Instruct-Q4_K_S.gguf && echo "✅ Llama GGUF found" || echo "❌ Llama GGUF NOT found"
docker exec ollama test -f /gguf_models/nomic-embed-text-v1.5.Q4_K_M.gguf && echo "✅ Nomic GGUF found" || echo "❌ Nomic GGUF NOT found"

# Try to create models directly with absolute paths
echo ""
echo "4. Creating llama3.2 from GGUF (using absolute path)..."
docker exec ollama sh -c 'cd /gguf_models && ollama create llama3.2 -f - <<EOF
FROM ./Llama-3.2-1B-Instruct-Q4_K_S.gguf
EOF'

echo ""
echo "5. Creating nomic-embed-text from GGUF (using absolute path)..."
docker exec ollama sh -c 'cd /gguf_models && ollama create nomic-embed-text -f - <<EOF
FROM ./nomic-embed-text-v1.5.Q4_K_M.gguf
EOF'

echo ""
echo "6. Listing models..."
docker exec ollama ollama list

echo ""
echo "✅ Test complete - NO NETWORK ACCESS USED"
