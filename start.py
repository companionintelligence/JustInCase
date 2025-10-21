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

def pull_ollama_model(model_name):
    """Pull an Ollama model"""
    print(f"Ensuring {model_name} model is available...")
    ollama_url = os.getenv("OLLAMA_URL", "http://ollama:11434")
    data = json.dumps({"name": model_name}).encode('utf-8')
    req = urllib.request.Request(
        f"{ollama_url}/api/pull",
        data=data,
        headers={'Content-Type': 'application/json'}
    )
    try:
        with urllib.request.urlopen(req) as response:
            print(f"Model {model_name} pull initiated")
    except Exception as e:
        print(f"Warning: Could not pull {model_name}: {e}")

def main():
    # Wait for services
    tika_url = os.getenv("TIKA_URL", "http://tika:9998")
    ollama_url = os.getenv("OLLAMA_URL", "http://ollama:11434")
    
    wait_for_service(tika_url, "Tika")
    wait_for_service(f"{ollama_url}/api/tags", "Ollama")
    
    # Pull models
    pull_ollama_model("llama3.2:1b")
    pull_ollama_model("nomic-embed-text")
    
    # Run ingestion if needed
    if not os.path.exists("data/index.faiss"):
        print("Running ingestion...")
        subprocess.run(["python3", "ingest.py", "pdfs"], check=True)
    
    # Start the server
    print("Starting server...")
    subprocess.run(["python3", "serve.py"], check=True)

if __name__ == "__main__":
    main()
