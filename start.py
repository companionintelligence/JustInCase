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
                    return True
        except (urllib.error.URLError, urllib.error.HTTPError):
            time.sleep(2)
    raise Exception(f"{service_name} failed to start after {max_retries} attempts")

def check_model_exists(model_name, ollama_url):
    """Check if a model exists in Ollama"""
    try:
        req = urllib.request.Request(f"{ollama_url}/api/tags")
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read())
            models = data.get('models', [])
            for model in models:
                model_full_name = model.get('name', '')
                # Check if the model name matches (with or without tag)
                if model_full_name == model_name or model_full_name.startswith(model_name + ':'):
                    print(f"Found model: {model_full_name}")
                    return True
    except Exception as e:
        print(f"Error checking models: {e}")
    return False

def pull_ollama_model(model_name, timeout=30):
    """Pull an Ollama model with timeout for slow connections"""
    print(f"Checking {model_name} model...")
    ollama_url = os.getenv("OLLAMA_URL", "http://ollama:11434")
    
    # First check if model already exists
    if check_model_exists(model_name, ollama_url):
        print(f"Model {model_name} already exists locally")
        return True
    
    # Skip pulling if SKIP_MODEL_PULL is set
    if os.getenv("SKIP_MODEL_PULL", "").lower() == "true":
        print(f"Skipping model pull for {model_name} (SKIP_MODEL_PULL=true)")
        return False
    
    # Pull the model with streaming to handle slow connections better
    data = json.dumps({"name": model_name}).encode('utf-8')
    req = urllib.request.Request(
        f"{ollama_url}/api/pull",
        data=data,
        headers={'Content-Type': 'application/json'}
    )
    
    print(f"Attempting to pull {model_name} model (timeout: {timeout}s)...")
    print(f"Note: Set SKIP_MODEL_PULL=true to skip this step")
    
    start_time = time.time()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            # Read the streaming response
            while True:
                if time.time() - start_time > timeout:
                    print(f"Timeout reached while pulling {model_name}")
                    return False
                    
                line = response.readline()
                if not line:
                    break
                try:
                    status = json.loads(line)
                    if 'status' in status:
                        print(f"  {status['status']}")
                    if status.get('status') == 'success':
                        print(f"Model {model_name} successfully pulled!")
                        return True
                except json.JSONDecodeError:
                    pass  # Skip non-JSON lines
    except Exception as e:
        print(f"Error pulling {model_name}: {e}")
        return False
    
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
    
    # Pull models only if needed
    models_needed = ["llama3.2:1b", "nomic-embed-text"]
    models_available = True
    
    for model in models_needed:
        if not pull_ollama_model(model, timeout=30):
            # Check if model exists anyway
            if not check_model_exists(model, ollama_url):
                print(f"ERROR: Model {model} is not available and could not be pulled.")
                models_available = False
    
    if not models_available:
        print("\n" + "="*60)
        print("IMPORTANT: Required models are not available!")
        print("Please run these commands manually when you have a better connection:")
        print("  docker exec -it ollama ollama pull llama3.2:1b")
        print("  docker exec -it ollama ollama pull nomic-embed-text")
        print("\nOr set SKIP_MODEL_PULL=true to skip automatic pulling")
        print("="*60 + "\n")
        raise Exception("Required models not available")
    
    # Run ingestion if needed
    if not os.path.exists("data/index.faiss"):
        print("Running ingestion...")
        subprocess.run(["python3", "ingest.py", "sources"], check=True)
    
    # Start the server
    print("Starting server...")
    subprocess.run(["python3", "serve.py"], check=True)

if __name__ == "__main__":
    main()
