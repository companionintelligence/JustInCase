#!/bin/bash

echo "🚀 Quick Setup Verification"
echo "=========================="
echo ""

# Check Docker
echo "1️⃣ Docker Status:"
if docker --version > /dev/null 2>&1; then
    echo "   ✅ Docker is installed: $(docker --version)"
else
    echo "   ❌ Docker is not installed or not in PATH"
    exit 1
fi

if docker ps > /dev/null 2>&1; then
    echo "   ✅ Docker daemon is running"
else
    echo "   ❌ Docker daemon is not running"
    exit 1
fi

# Check GGUF files
echo ""
echo "2️⃣ GGUF Model Files:"
if [ -d "./gguf_models" ]; then
    echo "   ✅ gguf_models directory exists"
    echo "   📁 Contents:"
    ls -lh ./gguf_models/*.gguf 2>/dev/null | awk '{print "      " $9 " (" $5 ")"}' || echo "      No .gguf files found"
else
    echo "   ❌ gguf_models directory not found"
fi

# Check Docker images
echo ""
echo "3️⃣ Docker Images:"
if docker images | grep -q "ghcr.io/ggerganov/llama.cpp"; then
    echo "   ✅ llama.cpp image is available locally"
else
    echo "   ⚠️  llama.cpp image not found locally (will be pulled on first run)"
fi

# Check if any containers are already running
echo ""
echo "4️⃣ Running Containers:"
if docker compose ps 2>/dev/null | grep -q "Up"; then
    echo "   ⚠️  Some containers are already running:"
    docker compose ps --format "table {{.Service}}\t{{.Status}}"
else
    echo "   ✅ No containers currently running"
fi

echo ""
echo "✅ Basic checks complete!"
echo ""
echo "To start the system: docker compose up"
