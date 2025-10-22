#!/usr/bin/env python3
"""
Llama.cpp server wrapper for JIC system
Provides Ollama-compatible API endpoints using llama.cpp Docker containers
"""
import os
import json
import requests
from flask import Flask, request, jsonify
from config import LLAMA_GGUF_FILE, NOMIC_GGUF_FILE

app = Flask(__name__)

# URLs for the llama.cpp containers
LLM_URL = os.getenv("LLM_URL", "http://llama-cpp-llm:8080")
EMBED_URL = os.getenv("EMBED_URL", "http://llama-cpp-embed:8080")

@app.route("/api/generate", methods=["POST"])
def generate():
    """Ollama-compatible generation endpoint"""
    data = request.json
    prompt = data.get("prompt", "")
    
    # Convert Ollama format to llama.cpp format
    llama_request = {
        "prompt": prompt,
        "n_predict": 512,
        "temperature": 0.7,
        "stop": ["</s>", "\n\n"],
        "stream": False
    }
    
    try:
        resp = requests.post(f"{LLM_URL}/completion", json=llama_request)
        if resp.status_code == 200:
            result = resp.json()
            return jsonify({"response": result.get("content", "")})
        else:
            return jsonify({"error": "LLM server error"}), 500
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/api/embed", methods=["POST"])
def embed():
    """Ollama-compatible embedding endpoint"""
    data = request.json
    text = data.get("input", "")
    
    try:
        resp = requests.post(f"{EMBED_URL}/embedding", 
                           json={"content": text})
        if resp.status_code == 200:
            result = resp.json()
            embedding = result.get("embedding", [])
            return jsonify({"embeddings": [embedding]})
        else:
            return jsonify({"error": "Embedding server error"}), 500
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/api/tags", methods=["GET"])
def list_models():
    """Ollama-compatible model listing endpoint"""
    models = [
        {"name": "llama3.2", "size": 1000000000},  # Approximate
        {"name": "nomic-embed-text", "size": 500000000}  # Approximate
    ]
    return jsonify({"models": models})

def main():
    # Start Flask wrapper
    print("Starting Ollama-compatible API wrapper on port 11434...")
    app.run(host="0.0.0.0", port=11434)

if __name__ == "__main__":
    main()
