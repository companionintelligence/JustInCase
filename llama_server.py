#!/usr/bin/env python3
"""
Llama.cpp server wrapper for JIC system
Provides Ollama-compatible API endpoints using llama.cpp
"""
import os
import json
import subprocess
import threading
import time
import requests
from flask import Flask, request, jsonify
from config import LLAMA_GGUF_FILE, NOMIC_GGUF_FILE

app = Flask(__name__)

# Global process handles
llm_process = None
embed_process = None

# Model paths
GGUF_DIR = "./gguf_models"
LLM_MODEL_PATH = os.path.join(GGUF_DIR, LLAMA_GGUF_FILE)
EMBED_MODEL_PATH = os.path.join(GGUF_DIR, NOMIC_GGUF_FILE)

# Ports for llama.cpp servers
LLM_PORT = 8081
EMBED_PORT = 8082

def start_llama_server(model_path, port, embedding_mode=False):
    """Start a llama.cpp server instance"""
    cmd = [
        "./llama.cpp/build/bin/llama-server",
        "-m", model_path,
        "--port", str(port),
        "--host", "0.0.0.0",
        "-c", "2048",  # context size
        "--n-gpu-layers", "-1",  # Use GPU if available
    ]
    
    if embedding_mode:
        cmd.extend(["--embedding", "--pooling", "mean"])
    
    print(f"Starting llama.cpp server: {' '.join(cmd)}")
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for server to be ready
    max_retries = 30
    for i in range(max_retries):
        try:
            resp = requests.get(f"http://localhost:{port}/health")
            if resp.status_code == 200:
                print(f"Server on port {port} is ready!")
                return process
        except:
            time.sleep(1)
    
    raise Exception(f"Server on port {port} failed to start")

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
        resp = requests.post(f"http://localhost:{LLM_PORT}/completion", json=llama_request)
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
        resp = requests.post(f"http://localhost:{EMBED_PORT}/embedding", 
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
    models = []
    
    if os.path.exists(LLM_MODEL_PATH):
        models.append({"name": "llama3.2", "size": os.path.getsize(LLM_MODEL_PATH)})
    
    if os.path.exists(EMBED_MODEL_PATH):
        models.append({"name": "nomic-embed-text", "size": os.path.getsize(EMBED_MODEL_PATH)})
    
    return jsonify({"models": models})

def main():
    global llm_process, embed_process
    
    # Check if models exist
    if not os.path.exists(LLM_MODEL_PATH):
        raise Exception(f"LLM model not found: {LLM_MODEL_PATH}")
    if not os.path.exists(EMBED_MODEL_PATH):
        raise Exception(f"Embedding model not found: {EMBED_MODEL_PATH}")
    
    # Build llama.cpp if needed
    if not os.path.exists("./llama.cpp/build/bin/llama-server"):
        print("Building llama.cpp...")
        subprocess.run(["./build-llama-cpp.sh"], check=True)
    
    # Start llama.cpp servers
    print("Starting LLM server...")
    llm_process = start_llama_server(LLM_MODEL_PATH, LLM_PORT)
    
    print("Starting embedding server...")
    embed_process = start_llama_server(EMBED_MODEL_PATH, EMBED_PORT, embedding_mode=True)
    
    # Start Flask wrapper
    print("Starting Ollama-compatible API wrapper on port 11434...")
    app.run(host="0.0.0.0", port=11434)

if __name__ == "__main__":
    try:
        main()
    finally:
        # Cleanup
        if llm_process:
            llm_process.terminate()
        if embed_process:
            embed_process.terminate()
