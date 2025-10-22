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

# Detect number of cores for parallel build
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    CORES=$(sysctl -n hw.ncpu)
else
    # Linux
    CORES=$(nproc)
fi

# Build
echo "Building with $CORES cores..."
mkdir -p build
cd build
cmake .. -DLLAMA_CURL=ON
cmake --build . --config Release -j $CORES

# Check if build succeeded
if [ ! -f "bin/llama-server" ]; then
    echo "❌ Build failed! llama-server binary not found."
    echo "Checking build directory contents:"
    ls -la
    exit 1
fi

echo "✅ Build complete!"
echo "Binary location: ./llama.cpp/build/bin/llama-server"
ls -la bin/
