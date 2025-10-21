#!/bin/bash

echo "🔍 JIC System Diagnostics"
echo "========================"

# Check if containers are running
echo -e "\n📦 Checking containers..."
if docker compose ps | grep -q "Up"; then
    echo "✅ Containers are running"
else
    echo "❌ Containers are not running. Run: docker compose up -d"
    exit 1
fi

# Check Ollama models
echo -e "\n🧠 Checking Ollama models..."
if docker compose exec -T ollama ollama list 2>/dev/null | grep -q "llama3.2:1b"; then
    echo "✅ llama3.2:1b model is available"
else
    echo "❌ llama3.2:1b model not found. Run: docker compose exec ollama ollama pull llama3.2:1b"
fi

if docker compose exec -T ollama ollama list 2>/dev/null | grep -q "nomic-embed-text"; then
    echo "✅ nomic-embed-text model is available"
else
    echo "❌ nomic-embed-text model not found. Run: docker compose exec ollama ollama pull nomic-embed-text"
fi

# Check Tika
echo -e "\n📄 Testing Tika..."
if [ -f "sample.pdf" ]; then
    curl -s --fail -T sample.pdf http://localhost:9998/tika > /dev/null && echo "✅ Tika is working" || echo "❌ Tika failed"
else
    echo "⚠️  No sample.pdf found, skipping Tika test"
fi

# Check FAISS index
echo -e "\n🔍 Checking FAISS index..."
if [ -f "data/index.faiss" ]; then
    echo "✅ FAISS index exists"
else
    echo "❌ FAISS index not found. It will be created on first run."
fi

# Test search functionality
echo -e "\n🔎 Testing search endpoint..."
response=$(curl -s -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"query": "How do I purify water?"}' 2>/dev/null)

if [ $? -eq 0 ] && echo "$response" | grep -q "answer"; then
    echo "✅ Search endpoint is working"
    if command -v jq &> /dev/null; then
        echo -e "\nSample response:"
        echo "$response" | jq -r '.answer' | head -n 3
    fi
else
    echo "❌ Search endpoint failed"
    echo "Response: $response"
fi

echo -e "\n✨ Diagnostics complete!"
