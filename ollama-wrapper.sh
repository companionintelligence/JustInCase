#!/bin/sh
# Wrapper script to ensure Ollama properly initializes with mounted models

echo "Starting Ollama with model initialization..."

# Start Ollama in the background
ollama serve &
OLLAMA_PID=$!

# Wait for Ollama to be ready
echo "Waiting for Ollama to start..."
for i in $(seq 1 30); do
    if ollama list >/dev/null 2>&1; then
        echo "Ollama is ready!"
        break
    fi
    sleep 1
done

# List models to trigger registration
echo "Checking models..."
ollama list

# Keep Ollama running
wait $OLLAMA_PID
