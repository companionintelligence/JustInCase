#!/bin/bash

echo "🔨 Building llama.cpp"
echo "===================="

# Clone llama.cpp if not present
if [ ! -d "llama.cpp" ]; then
    echo "Cloning llama.cpp (shallow clone for speed)..."
    git clone --depth 1 https://github.com/ggerganov/llama.cpp.git
fi

cd llama.cpp

# Update to latest
echo "Updating llama.cpp..."
git pull

# Build
echo "Building..."
mkdir -p build
cd build
cmake .. -DLLAMA_CURL=ON
cmake --build . --config Release -j $(nproc)

echo "✅ Build complete!"
echo "Binary location: ./llama.cpp/build/bin/llama-server"
