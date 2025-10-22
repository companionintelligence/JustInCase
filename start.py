#!/usr/bin/env python3
import time
import urllib.request
import urllib.error
import json
import subprocess
import os
import threading
from config import LLM_MODEL, EMBEDDING_MODEL, TIKA_URL, LLM_URL, EMBED_URL, LLAMA_GGUF_FILE, NOMIC_GGUF_FILE

def wait_for_service(url, service_name, max_retries=30):
    """Wait for a service to be ready"""
    print(f"Waiting for {service_name}...")
    for i in range(max_retries):
        try:
            with urllib.request.urlopen(url, timeout=2) as response:
                if response.status == 200:
                    print(f"{service_name} is ready!")
                    return True
        except (urllib.error.URLError, urllib.error.HTTPError):
            time.sleep(2)
    raise Exception(f"{service_name} failed to start after {max_retries} attempts")


def check_gguf_models():
    """Check if GGUF model files exist"""
    gguf_dir = "./gguf_models"
    llm_path = os.path.join(gguf_dir, LLAMA_GGUF_FILE)
    embed_path = os.path.join(gguf_dir, NOMIC_GGUF_FILE)
    
    missing = []
    if not os.path.exists(llm_path):
        missing.append(f"LLM model: {LLAMA_GGUF_FILE}")
    if not os.path.exists(embed_path):
        missing.append(f"Embedding model: {NOMIC_GGUF_FILE}")
    
    if missing:
        print("\n" + "="*60)
        print("FATAL: Required GGUF models not found!")
        print("Missing files in ./gguf_models/:")
        for m in missing:
            print(f"  - {m}")
        print("\nPlease download the GGUF files and place them in ./gguf_models/")
        print("See ./fetch-models.sh for download instructions")
        print("="*60 + "\n")
        raise Exception("Required GGUF models not found")
    
    print("✅ All required GGUF models found")

def main():
    # Check GGUF models first
    check_gguf_models()
    
    # Wait for services
    wait_for_service(TIKA_URL, "Tika")
    
    # Wait for llama.cpp containers
    wait_for_service(f"{LLM_URL}/health", "LLM server")
    wait_for_service(f"{EMBED_URL}/health", "Embedding server")
    
    # Run ingestion if needed
    if not os.path.exists("data/index.faiss"):
        print("Running ingestion...")
        print(f"Current directory: {os.getcwd()}")
        print(f"Directory contents: {os.listdir('.')}")
        
        # Check if sources directory exists
        if os.path.exists("sources"):
            print(f"Sources directory exists. Contents: {os.listdir('sources')}")
        else:
            print("ERROR: Sources directory not found!")
        
        try:
            result = subprocess.run(["python3", "ingest.py", "sources"], 
                                  capture_output=True, text=True, check=True)
            print("Ingestion output:", result.stdout)
            if result.stderr:
                print("Ingestion errors:", result.stderr)
        except subprocess.CalledProcessError as e:
            print(f"Ingestion failed with exit code {e.returncode}")
            print(f"STDOUT: {e.stdout}")
            print(f"STDERR: {e.stderr}")
            raise
    
    # Start the main server
    print("Starting main server...")
    subprocess.run(["python3", "serve.py"], check=True)

if __name__ == "__main__":
    main()
