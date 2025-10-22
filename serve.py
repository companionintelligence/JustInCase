import os
import faiss
import json
import traceback
import threading
import time
from pathlib import Path
from flask import Flask, request, jsonify, send_from_directory
import requests
from llama_cpp import Llama
from config import LLM_MODEL, EMBEDDING_MODEL, MAX_CONTEXT_CHUNKS, SEARCH_TOP_K, TIKA_URL, CHUNK_SIZE, CHUNK_OVERLAP, NOMIC_GGUF_FILE, LLAMA_GGUF_FILE
import logging

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# Initialize models
embedding_model = None
llm_model = None

def init_models():
    """Initialize both the LLM and embedding models"""
    global embedding_model, llm_model
    
    # Initialize embedding model
    embed_path = f"./gguf_models/{NOMIC_GGUF_FILE}"
    if not os.path.exists(embed_path):
        logger.error(f"Embedding model not found at {embed_path}")
        raise FileNotFoundError(f"Missing embedding model: {embed_path}")
    
    logger.info(f"Loading embedding model from {embed_path}...")
    try:
        embedding_model = Llama(
            model_path=embed_path,
            embedding=True,
            n_ctx=8192,  # Context size for nomic
            n_threads=4,  # Adjust based on your CPU
            verbose=False
        )
        logger.info("Embedding model loaded successfully")
    except Exception as e:
        logger.error(f"Failed to load embedding model: {e}")
        raise
    
    # Initialize LLM model
    llm_path = f"./gguf_models/{LLAMA_GGUF_FILE}"
    if not os.path.exists(llm_path):
        logger.error(f"LLM model not found at {llm_path}")
        raise FileNotFoundError(f"Missing LLM model: {llm_path}")
    
    logger.info(f"Loading LLM model from {llm_path}...")
    try:
        llm_model = Llama(
            model_path=llm_path,
            n_ctx=2048,  # Context size
            n_threads=4,  # Adjust based on your CPU
            verbose=False
        )
        logger.info("LLM model loaded successfully")
    except Exception as e:
        logger.error(f"Failed to load LLM model: {e}")
        raise

# Initialize models on startup
init_models()

app = Flask(__name__)

# Global variables for tracking ingestion
ingestion_status = {
    "in_progress": False,
    "total_files": 0,
    "files_processed": 0,
    "current_file": None,
    "start_time": None,
    "last_update": None
}
index_lock = threading.Lock()

def get_embedding(text):
    """Get embedding using llama.cpp directly"""
    try:
        if embedding_model is None:
            init_models()
        
        # Get embedding from llama.cpp
        embedding = embedding_model.embed(text)
        logger.debug(f"Embedding dimension: {len(embedding)}")
        return embedding
    except Exception as e:
        logger.error(f"Error getting embedding: {e}")
        raise

# Global variables for index and docs
index = None
docs = []

def load_index():
    """Load FAISS index and metadata"""
    global index, docs
    try:
        if os.path.exists("data/index.faiss") and os.path.exists("data/metadata.jsonl"):
            with index_lock:
                index = faiss.read_index("data/index.faiss")
                logger.info(f"Loaded FAISS index with dimension: {index.d}")
                with open("data/metadata.jsonl") as f:
                    docs = [json.loads(line) for line in f]
                logger.info(f"Loaded {len(docs)} documents")
        else:
            logger.warning("No existing index found")
            # Create empty index with expected dimension
            dimension = 384  # nomic-embed-text dimension
            index = faiss.IndexFlatL2(dimension)
            docs = []
    except Exception as e:
        logger.error(f"Error loading index: {e}")
        # Create empty index as fallback
        dimension = 384
        index = faiss.IndexFlatL2(dimension)
        docs = []

def split_text(text, chunk_size=CHUNK_SIZE, chunk_overlap=CHUNK_OVERLAP):
    """Split text into overlapping chunks."""
    chunks = []
    start = 0
    text_len = len(text)
    
    while start < text_len:
        end = start + chunk_size
        
        if end < text_len:
            # Try to break at sentence boundaries
            for sep in ['. ', '! ', '? ', '\n\n', '\n', ' ']:
                sep_pos = text.rfind(sep, start + chunk_size - 100, end)
                if sep_pos != -1:
                    end = sep_pos + len(sep)
                    break
        
        chunks.append(text[start:end])
        start = end - chunk_overlap
    
    return chunks

def get_embeddings_batch(texts):
    """Get embeddings for multiple texts at once"""
    try:
        # llama.cpp doesn't support batch embeddings, so we process one by one
        embeddings = []
        for text in texts:
            embeddings.append(get_embedding(text))
        return embeddings
    except Exception as e:
        logger.error(f"Error getting batch embeddings: {e}")
        raise

def background_ingestion():
    """Background task to ingest documents"""
    global index, docs, ingestion_status
    
    sources_dir = "sources"
    
    # Wait a bit for services to be ready
    time.sleep(5)
    
    # Track processed files to avoid re-processing
    processed_files = set()
    if os.path.exists("data/processed_files.txt"):
        with open("data/processed_files.txt", "r") as f:
            processed_files = set(line.strip() for line in f)
    
    while True:
        try:
            # Find all files in sources
            all_files = []
            for root, dirs, files in os.walk(sources_dir):
                for fname in files:
                    if not fname.startswith('.') and fname.endswith(('.pdf', '.txt')):
                        full_path = os.path.join(root, fname)
                        rel_path = os.path.relpath(full_path, sources_dir)
                        if rel_path not in processed_files:
                            all_files.append((full_path, rel_path))
            
            if all_files:
                ingestion_status["in_progress"] = True
                ingestion_status["total_files"] = len(all_files)
                ingestion_status["files_processed"] = 0
                ingestion_status["start_time"] = time.time()
                
                logger.info(f"Starting ingestion of {len(all_files)} new files")
                
                embeddings = []
                new_docs = []
                
                for full_path, rel_path in all_files:
                    ingestion_status["current_file"] = rel_path
                    
                    try:
                        # Extract text
                        if full_path.endswith(".txt"):
                            with open(full_path, "r", encoding="utf-8") as f:
                                raw_text = f.read().strip()
                        else:
                            # Use Tika for PDFs
                            with open(full_path, "rb") as f:
                                resp = requests.put(f"{TIKA_URL}/tika", 
                                                  headers={"Accept": "text/plain"}, 
                                                  data=f)
                                if resp.status_code == 200:
                                    raw_text = resp.text.strip()
                                else:
                                    logger.warning(f"Tika couldn't process {rel_path}")
                                    continue
                        
                        # Split into chunks
                        chunks = split_text(raw_text)
                        chunk_count = 0
                        
                        for i, chunk in enumerate(chunks):
                            chunk = chunk.strip()
                            if len(chunk) > 100:  # Skip small chunks
                                # Get embedding
                                embedding = get_embedding(chunk)
                                embeddings.append(embedding)
                                new_docs.append({"filename": rel_path, "text": chunk})
                                chunk_count += 1
                                
                                # Update progress more frequently
                                if i % 5 == 0:  # Every 5 chunks
                                    ingestion_status["last_update"] = time.time()
                        
                        ingestion_status["files_processed"] += 1
                        ingestion_status["last_update"] = time.time()
                        logger.info(f"Processed {rel_path}: {chunk_count} chunks added from {len(chunks)} total")
                        
                    except Exception as e:
                        logger.error(f"Error processing {rel_path}: {e}")
                
                # Update index with all new embeddings
                if embeddings:
                    with index_lock:
                        # FAISS accepts list of lists directly
                        index.add(embeddings)
                        docs.extend(new_docs)
                        
                        # Save to disk
                        faiss.write_index(index, "data/index.faiss")
                        with open("data/metadata.jsonl", "w") as f:
                            for doc in docs:
                                f.write(json.dumps(doc) + "\n")
                        
                        # Update processed files list
                        processed_files.update(rel_path for _, rel_path in all_files[:ingestion_status["files_processed"]])
                        with open("data/processed_files.txt", "w") as f:
                            for pf in processed_files:
                                f.write(pf + "\n")
                    
                    logger.info(f"Added {len(embeddings)} new embeddings. Total: {len(docs)}")
                
                ingestion_status["in_progress"] = False
                ingestion_status["current_file"] = None
                
        except Exception as e:
            logger.error(f"Error in background ingestion: {e}")
            ingestion_status["in_progress"] = False
        
        # Check for new files every 30 seconds
        time.sleep(30)

# Initialize index on startup
load_index()

# Start background ingestion thread
ingestion_thread = threading.Thread(target=background_ingestion, daemon=True)
ingestion_thread.start()

@app.route("/query", methods=["POST"])
def query():
    logger.info("Query Handler Started")
    try:
        q = request.json.get("query", "").strip()
        logger.info(f"Received query: '{q}'")

        if not q:
            logger.warning("Empty query received.")
            return jsonify({"error": "Empty query"}), 400

        # Even with no documents, let the LLM try to answer
        if len(docs) == 0:
            # Use local LLM model
            prompt = f"Question: {q}\n\nAnswer:"
            response = llm_model(
                prompt,
                max_tokens=1024,
                temperature=0.7,
                stop=["</s>", "Question:"],
                echo=False
            )
            
            reply = response['choices'][0]['text'].strip()
            result = {
                "answer": reply, 
                "matches": [],
                "status": ingestion_status
            }

            return jsonify(result)

        # Encode query
        embedding = get_embedding(q)
        
        # FAISS accepts list of lists directly
        qvec = [embedding]
        
        logger.info(f"Query embedding dimension: {len(embedding)}, Index dimension: {index.d}")

        # Perform FAISS search with lock
        with index_lock:
            D, I = index.search(qvec, k=min(SEARCH_TOP_K, len(docs)))
        
        logger.info(f"FAISS search results - distances: {D[0][:5]}, indices: {I[0][:5]}")

        # Filter valid indices and limit context chunks
        valid_indices = [i for i in I[0] if i != -1 and i < len(docs)]
        selected_chunks = valid_indices[:MAX_CONTEXT_CHUNKS]

        if not selected_chunks:
            logger.warning("No relevant documents found.")
            return jsonify({
                "answer": "I couldn't find relevant information for your query.",
                "matches": [],
                "status": ingestion_status
            }), 200

        # Build concise context
        context = "\n\n".join(docs[i]["text"] for i in selected_chunks)
        logger.info(f"Generated context length: {len(context)} chars")

        # Use local LLM model
        prompt = f"Context: {context}\n\nQuestion: {q}\n\nAnswer:"
        response = llm_model(
            prompt,
            max_tokens=1024,
            temperature=0.7,
            stop=["</s>", "Question:"],
            echo=False
        )
        
        reply = response['choices'][0]['text'].strip()
        result = {
            "answer": reply, 
            "matches": [docs[i] for i in selected_chunks],
            "status": ingestion_status
        }

        return jsonify(result)

    except Exception as e:
        logger.error(f"Exception in query handler: {str(e)}")
        traceback.print_exc()
        return jsonify({"error": str(e), "traceback": traceback.format_exc()}), 500

@app.route("/status", methods=["GET"])
def status():
    """Get system status including ingestion progress"""
    status_info = {
        "documents_indexed": len(docs),
        "ingestion": ingestion_status
    }
    
    # Calculate progress percentage
    if ingestion_status["in_progress"] and ingestion_status["total_files"] > 0:
        status_info["progress_percent"] = int(
            (ingestion_status["files_processed"] / ingestion_status["total_files"]) * 100
        )
    else:
        status_info["progress_percent"] = 100 if len(docs) > 0 else 0
    
    return jsonify(status_info)

BASE_DIR = os.path.abspath(os.path.dirname(__file__))


@app.route("/")
def serve_ui():
    return send_from_directory(".", "index.html")

@app.route("/<path:filename>")
def serve_any_file(filename):
    full_path = os.path.join(BASE_DIR, filename)
    if os.path.isfile(full_path):
        return send_from_directory(BASE_DIR, filename)
    return "Not found", 404

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)

