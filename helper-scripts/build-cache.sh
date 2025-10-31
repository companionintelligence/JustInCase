#!/bin/bash
# Script to build with Docker cache optimization

echo "🚀 Optimized build with caching"
echo "=============================="

# Use BuildKit for better caching
export DOCKER_BUILDKIT=1
export BUILDKIT_PROGRESS=plain

# Build with cache mount for faster rebuilds
docker build \
    --target app-builder \
    --cache-from jic:llama-cache \
    --build-arg BUILDKIT_INLINE_CACHE=1 \
    -t jic:build-cache \
    .

# Tag the llama builder stage for caching
docker build \
    --target llama-builder \
    --cache-from jic:llama-cache \
    --build-arg BUILDKIT_INLINE_CACHE=1 \
    -t jic:llama-cache \
    .

# Build the final image
docker compose build

echo ""
echo "✅ Build complete!"
echo ""
echo "The llama.cpp library is now cached in jic:llama-cache"
echo "Future builds will only recompile server.cpp"
