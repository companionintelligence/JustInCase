#!/bin/bash
# Build and run the C++ version

echo "Building C++ server..."

# Build the C++ version
docker compose --profile cpp build

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo ""
    echo "To run the C++ server:"
    echo "  docker compose --profile cpp up"
    echo ""
    echo "To run the Python server instead:"
    echo "  docker compose --profile python up"
else
    echo "❌ Build failed"
    exit 1
fi
