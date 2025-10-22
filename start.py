#!/usr/bin/env python3
import time
import urllib.request
import urllib.error
import json
import subprocess
import os
from config import LLM_MODEL, EMBEDDING_MODEL, TIKA_URL, OLLAMA_URL

def wait_for_service(url, service_name, max_retries=30):
    """Wait for a service to be ready"""
    print(f"Waiting for {service_name}...")
    for i in range(max_retries):
        try:
            with urllib.request.urlopen(url, timeout=2) as response:
                if response.status == 200:
                    print(f"{service_name} is ready!")
                    # Give Ollama extra time to fully initialize
                    if "ollama" in service_name.lower():
                        print("Giving Ollama extra time to initialize and scan models...")
                        time.sleep(10)
                    return True
        except (urllib.error.URLError, urllib.error.HTTPError):
            time.sleep(2)
    raise Exception(f"{service_name} failed to start after {max_retries} attempts")

def check_model_exists(model_name, ollama_url, retry_count=3):
    """Check if a model exists in Ollama with retries"""
    for attempt in range(retry_count):
        try:
            req = urllib.request.Request(f"{ollama_url}/api/tags")
            with urllib.request.urlopen(req, timeout=10) as response:
                data = json.loads(response.read())
                models = data.get('models', [])
                model_names = [m.get('name', '') for m in models]
                
                # Extract base name without tag for comparison
                base_name = model_name.split(':')[0]
                
                for model_full_name in model_names:
                    # Check exact match
                    if model_full_name == model_name:
                        print(f"✅ Found model: {model_full_name}")
                        return True
                    # Check if base names match (e.g., llama3.2:1b matches llama3.2:latest)
                    if model_full_name.startswith(base_name + ':'):
                        print(f"✅ Found model: {model_full_name} (matches requested {model_name})")
                        return True
                
                # If we got a response but model not found, no need to retry
                if attempt == 0:
                    print(f"Model {model_name} not in list: {model_names}")
                return False
        except Exception as e:
            if attempt < retry_count - 1:
                print(f"Error checking models (attempt {attempt + 1}/{retry_count}): {e}")
                time.sleep(2)
            else:
                print(f"Failed to check models after {retry_count} attempts: {e}")
    return False


def main():
    # Wait for services
    wait_for_service(TIKA_URL, "Tika")
    wait_for_service(f"{OLLAMA_URL}/api/tags", "Ollama")
    
    
    # List available models for debugging
    try:
        req = urllib.request.Request(f"{OLLAMA_URL}/api/tags")
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read())
            available_models = [m.get('name', '') for m in data.get('models', [])]
            print(f"Available Ollama models: {available_models}")
    except Exception as e:
        print(f"Could not list models: {e}")
    
    # Check if we should skip model verification
    if os.getenv("SKIP_MODEL_CHECK", "").lower() == "true":
        print("\n" + "="*60)
        print("SKIP_MODEL_CHECK=true - Skipping model verification")
        print("Assuming models are available...")
        print("="*60)
    else:
        # Check models - NEVER pull, only verify
        models_needed = [LLM_MODEL, EMBEDDING_MODEL]
        missing_models = []
        
        print("\n" + "="*60)
        print("Verifying required models...")
        print("="*60)
        
        for model in models_needed:
            if check_model_exists(model, OLLAMA_URL, retry_count=5):
                print(f"✅ Model {model} is available")
            else:
                print(f"❌ Model {model} NOT FOUND")
                missing_models.append(model)
        
        if missing_models:
            print("\n" + "="*60)
            print("FATAL: Required models are not available!")
            print("Missing models:", ", ".join(missing_models))
            print("\nModels must be loaded from GGUF files before running Docker.")
            print("Instructions:")
            print("1. Download GGUF files manually")
            print("2. Place them in ./gguf_models/")
            print("3. Run ./prepare-gguf-models.sh")
            print("4. Run ./load-gguf-models.sh")
            print("\nOr skip model checks with: SKIP_MODEL_CHECK=true")
            print("="*60 + "\n")
            raise Exception(f"Required models not available: {', '.join(missing_models)}")
    
    # Run ingestion if needed
    if not os.path.exists("data/index.faiss"):
        print("Running ingestion...")
        subprocess.run(["python3", "ingest.py", "sources"], check=True)
    
    # Start the server
    print("Starting server...")
    subprocess.run(["python3", "serve.py"], check=True)

if __name__ == "__main__":
    main()
