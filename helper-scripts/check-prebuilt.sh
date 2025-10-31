#!/bin/bash
# Check for prebuilt llama.cpp binaries

ARCH=$(uname -m)
OS=$(uname -s | tr '[:upper:]' '[:lower:]')

echo "Checking for prebuilt binaries..."
echo "Architecture: $ARCH"
echo "OS: $OS"

# Map architecture names
case $ARCH in
    x86_64)
        LLAMA_ARCH="x64"
        ;;
    aarch64|arm64)
        LLAMA_ARCH="arm64"
        ;;
    *)
        echo "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

# Check latest release
LATEST_RELEASE=$(curl -s https://api.github.com/repos/ggml-org/llama.cpp/releases/latest | grep -o '"tag_name": "[^"]*' | cut -d'"' -f4)
echo "Latest llama.cpp release: $LATEST_RELEASE"

# Construct download URL
DOWNLOAD_URL="https://github.com/ggml-org/llama.cpp/releases/download/${LATEST_RELEASE}/llama-${LATEST_RELEASE}-bin-ubuntu-${LLAMA_ARCH}.zip"

echo "Checking for: $DOWNLOAD_URL"

# Check if the file exists
if curl --head --silent --fail "$DOWNLOAD_URL" > /dev/null; then
    echo "✅ Prebuilt binaries available for your architecture!"
    echo "Download with: wget $DOWNLOAD_URL"
else
    echo "❌ No prebuilt binaries available for $OS-$LLAMA_ARCH"
    echo "You'll need to build from source."
fi
