#!/bin/bash

echo "🔧 Bypass Ollama Model Check"
echo "============================"
echo ""
echo "This will start the system without model verification."
echo "Use this if you KNOW the models are present but Ollama can't see them."
echo ""

# Set environment to skip model checks
export SKIP_MODEL_CHECK=true

echo "Starting with SKIP_MODEL_CHECK=true..."
docker compose up --build
