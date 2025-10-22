#!/bin/bash
# Build the JIC C++ server

echo "Building JIC server..."

# Build the server
docker compose build

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo ""
    echo "To run the server:"
    echo "  docker compose up"
else
    echo "❌ Build failed"
    exit 1
fi
