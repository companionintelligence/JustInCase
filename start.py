#!/usr/bin/env python3
import time
import urllib.request
import urllib.error
import json
import subprocess
import os

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
                        print("Giving Ollama extra time to initialize...")
                        time.sleep(3)
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
    tika_url = os.getenv("TIKA_URL", "http://tika:9998")
    ollama_url = os.getenv("OLLAMA_URL", "http://ollama:11434")
    
    wait_for_service(tika_url, "Tika")
    wait_for_service(f"{ollama_url}/api/tags", "Ollama")
    
    
    # List available models for debugging
    try:
        req = urllib.request.Request(f"{ollama_url}/api/tags")
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read())
            available_models = [m.get('name', '') for m in data.get('models', [])]
            print(f"Available Ollama models: {available_models}")
    except Exception as e:
        print(f"Could not list models: {e}")
    
    # Verify models are available - no pulling, just checking
    models_needed = ["llama3.2:1b", "nomic-embed-text"]
    missing_models = []
    
    print("\n" + "="*60)
    print("Verifying required models...")
    print("="*60)
    
    for model in models_needed:
        if check_model_exists(model, ollama_url, retry_count=5):
            print(f"✅ Model {model} is available")
        else:
            print(f"❌ Model {model} NOT FOUND")
            missing_models.append(model)
    
    if missing_models:
        print("\n" + "="*60)
        print("FATAL: Required models are not available!")
        print("Missing models:", ", ".join(missing_models))
        print("\nModels must be pre-downloaded before building Docker.")
        print("Run: ./download-models-local.sh")
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
