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

# Get model names from config
LLM_MODEL=$(docker compose exec -T survival-rag python3 -c "from config import LLM_MODEL; print(LLM_MODEL)" 2>/dev/null || echo "llama3.2")
EMBEDDING_MODEL=$(docker compose exec -T survival-rag python3 -c "from config import EMBEDDING_MODEL; print(EMBEDDING_MODEL)" 2>/dev/null || echo "nomic-embed-text")

# Check llama.cpp models
echo -e "\n🧠 Checking llama.cpp models..."
response=$(curl -s http://localhost:11434/api/tags 2>/dev/null)
if [ $? -eq 0 ]; then
    if echo "$response" | grep -q "$LLM_MODEL"; then
        echo "✅ $LLM_MODEL model is available"
    else
        echo "❌ $LLM_MODEL model not found"
    fi
    
    if echo "$response" | grep -q "$EMBEDDING_MODEL"; then
        echo "✅ $EMBEDDING_MODEL model is available"
    else
        echo "❌ $EMBEDDING_MODEL model not found"
    fi
else
    echo "❌ Could not connect to llama.cpp server"
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
