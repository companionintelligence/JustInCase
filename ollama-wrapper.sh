#!/bin/sh
# Wrapper script to ensure Ollama properly initializes with mounted models

echo "Starting Ollama with model initialization..."

# Check what's in the mounted directory
echo "Checking mounted models directory..."
if [ -d "/root/.ollama/models" ]; then
    echo "Models directory exists"
    ls -la /root/.ollama/models/
else
    echo "WARNING: No models directory found at /root/.ollama/models"
fi

# Start Ollama
exec ollama serve
