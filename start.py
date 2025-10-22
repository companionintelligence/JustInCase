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

def pull_ollama_model(model_name, timeout=300):
    """Pull an Ollama model with timeout for slow connections"""
    print(f"\n📥 Pulling {model_name} model...")
    ollama_url = os.getenv("OLLAMA_URL", "http://ollama:11434")
    
    # First check if model already exists with retries
    for retry in range(3):
        if check_model_exists(model_name, ollama_url):
            print(f"✅ Model {model_name} already exists locally")
            return True
        if retry < 2:
            print(f"Model check attempt {retry + 1} failed, retrying in 2 seconds...")
            time.sleep(2)
    
    # Skip pulling if SKIP_MODEL_PULL is set
    if os.getenv("SKIP_MODEL_PULL", "").lower() == "true":
        print(f"❌ Model {model_name} not found and SKIP_MODEL_PULL=true")
        return False
    
    # Pull the model with streaming to handle slow connections better
    data = json.dumps({"name": model_name, "stream": True}).encode('utf-8')
    req = urllib.request.Request(
        f"{ollama_url}/api/pull",
        data=data,
        headers={'Content-Type': 'application/json'}
    )
    
    print(f"Attempting to pull {model_name} (this may take several minutes)...")
    print(f"Note: Set SKIP_MODEL_PULL=true to skip this step")
    
    start_time = time.time()
    last_progress_time = start_time
    last_progress_percent = 0
    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            # Read the streaming response
            while True:
                current_time = time.time()
                if current_time - start_time > timeout:
                    print(f"Timeout reached while pulling {model_name} after {timeout}s")
                    # Check one more time if model exists
                    if check_model_exists(model_name, ollama_url):
                        print(f"Model {model_name} found despite timeout!")
                        return True
                    return False
                
                # Check for timeout without progress
                if current_time - last_progress_time > 60:
                    print(f"⚠️  No progress for 60 seconds...")
                    # Don't give up yet, the download might still be working
                    
                try:
                    line = response.readline()
                    if not line:
                        break
                except Exception as e:
                    print(f"Error reading response: {e}")
                    break
                    
                try:
                    status = json.loads(line)
                    if 'status' in status:
                        status_msg = status['status']
                        # Show download progress if available
                        if 'completed' in status and 'total' in status and status['total'] > 0:
                            pct = (status['completed'] / status['total']) * 100
                            # Only print if progress changed significantly
                            if pct - last_progress_percent >= 5 or pct >= 100:
                                print(f"  📊 {status_msg} - {pct:.1f}%")
                                last_progress_percent = pct
                                last_progress_time = current_time
                        elif 'digest' in status_msg.lower() or 'pulling' in status_msg.lower():
                            # Show important status messages
                            print(f"  ⏳ {status_msg}")
                            last_progress_time = current_time
                    if status.get('status') == 'success':
                        print(f"✅ Model {model_name} successfully pulled!")
                        # Give Ollama a moment to register the model
                        time.sleep(2)
                        return True
                except json.JSONDecodeError:
                    pass  # Skip non-JSON lines
    except urllib.error.URLError as e:
        if "timed out" in str(e):
            print(f"⚠️  Connection timeout while pulling {model_name}")
            print("Checking if model was downloaded anyway...")
            time.sleep(2)
            if check_model_exists(model_name, ollama_url):
                print(f"✅ Model {model_name} found despite timeout!")
                return True
        print(f"❌ Error pulling {model_name}: {e}")
        return False
    except Exception as e:
        print(f"❌ Error pulling {model_name}: {e}")
        return False
    
    # Final check if model exists (with extra retries)
    print("Performing final check for model...")
    for retry in range(5):
        if check_model_exists(model_name, ollama_url):
            print(f"✅ Model {model_name} confirmed available!")
            return True
        if retry < 4:
            print(f"Model not found yet, waiting 2 seconds... (attempt {retry + 1}/5)")
            time.sleep(2)
    
    print(f"❌ Model {model_name} not found after pull attempt")
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
    
    # Check if we should skip model operations entirely
    if os.getenv("SKIP_MODEL_CHECK", "").lower() == "true":
        print("SKIP_MODEL_CHECK=true, assuming models are available")
    else:
        # Pull models only if needed
        models_needed = ["llama3.2:1b", "nomic-embed-text"]
        missing_models = []
        
        print("\n" + "="*60)
        print("Checking for required models...")
        print("="*60)
        
        for model in models_needed:
            # First, thoroughly check if model already exists
            if check_model_exists(model, ollama_url, retry_count=5):
                print(f"✅ Model {model} is already available locally")
                continue
            
            # Model doesn't exist, try to pull it
            if os.getenv("SKIP_MODEL_PULL", "").lower() == "true":
                print(f"❌ Model {model} not found and SKIP_MODEL_PULL=true")
                missing_models.append(model)
            else:
                print(f"Model {model} not found locally, attempting to pull...")
                pull_success = pull_ollama_model(model, timeout=300)
                
                # Check again after pull attempt
                if not check_model_exists(model, ollama_url):
                    print(f"ERROR: Model {model} is still not available after pull attempt.")
                    missing_models.append(model)
        
        if missing_models:
            print("\n" + "="*60)
            print("IMPORTANT: Required models are not available!")
            print("Missing models:", ", ".join(missing_models))
            print("\nOptions:")
            print("1. Pull models manually when you have a better connection:")
            for model in missing_models:
                print(f"   docker compose exec ollama ollama pull {model}")
            print("\n2. If you know the models are present, skip all model checks:")
            print("   SKIP_MODEL_CHECK=true docker compose up")
            print("\n3. Skip automatic model pulling:")
            print("   SKIP_MODEL_PULL=true docker compose up")
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
