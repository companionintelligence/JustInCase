#!/bin/bash

# This script runs inside the Ollama container to ensure models are registered

echo "Initializing Ollama models..."

# Wait for Ollama to be ready
for i in {1..30}; do
    if ollama list >/dev/null 2>&1; then
        break
    fi
    sleep 1
done

# List current models
echo "Current models:"
ollama list

# Check if models exist in the filesystem but aren't registered
if [ -d "/root/.ollama/models/manifests" ]; then
    echo "Model manifests found in filesystem"
    
    # Force Ollama to rescan models by listing them
    ollama list >/dev/null 2>&1
    
    # Give it a moment to register
    sleep 2
    
    # List again
    echo "Models after rescan:"
    ollama list
fi
