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
                if model.get('name') == model_name:
                    return True
    except Exception as e:
        print(f"Error checking models: {e}")
    return False

def pull_ollama_model(model_name):
    """Pull an Ollama model and wait for completion"""
    print(f"Checking {model_name} model...")
    ollama_url = os.getenv("OLLAMA_URL", "http://ollama:11434")
    
    # First check if model already exists
    if check_model_exists(model_name, ollama_url):
        print(f"Model {model_name} already exists locally")
        return
    
    # Pull the model with streaming to handle slow connections better
    data = json.dumps({"name": model_name}).encode('utf-8')
    req = urllib.request.Request(
        f"{ollama_url}/api/pull",
        data=data,
        headers={'Content-Type': 'application/json'}
    )
    
    print(f"Pulling {model_name} model (this may take a while on slow connections)...")
    print(f"Note: The model will be cached in the ollama_data volume for future use")
    
    try:
        with urllib.request.urlopen(req) as response:
            # Read the streaming response
            while True:
                line = response.readline()
                if not line:
                    break
                try:
                    status = json.loads(line)
                    if 'status' in status:
                        print(f"  {status['status']}")
                    if status.get('status') == 'success':
                        print(f"Model {model_name} successfully pulled!")
                        return
                except json.JSONDecodeError:
                    pass  # Skip non-JSON lines
    except Exception as e:
        print(f"Error pulling {model_name}: {e}")
        print("You may need to manually pull the model with: docker exec -it ollama ollama pull " + model_name)
        raise

def main():
    # Wait for services
    tika_url = os.getenv("TIKA_URL", "http://tika:9998")
    ollama_url = os.getenv("OLLAMA_URL", "http://ollama:11434")
    
    wait_for_service(tika_url, "Tika")
    wait_for_service(f"{ollama_url}/api/tags", "Ollama")
    
    # Pull models only if needed
    try:
        pull_ollama_model("llama3.2:1b")
        pull_ollama_model("nomic-embed-text")
    except Exception as e:
        print(f"Warning: Model pulling failed: {e}")
        print("Checking if models are already available...")
        if not (check_model_exists("llama3.2:1b", ollama_url) and 
                check_model_exists("nomic-embed-text", ollama_url)):
            print("ERROR: Required models are not available. Cannot continue.")
            raise
        print("Models are available, continuing...")
    
    # Run ingestion if needed
    if not os.path.exists("data/index.faiss"):
        print("Running ingestion...")
        subprocess.run(["python3", "ingest.py", "sources"], check=True)
    
    # Start the server
    print("Starting server...")
    subprocess.run(["python3", "serve.py"], check=True)

if __name__ == "__main__":
    main()
