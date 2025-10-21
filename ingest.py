#!/usr/bin/env python3
import os
import sys
import json
import faiss
import requests

# Simple text splitter function
def split_text(text, chunk_size=500, chunk_overlap=50):
    """Split text into overlapping chunks."""
    chunks = []
    start = 0
    text_len = len(text)
    
    while start < text_len:
        # Find the end of the chunk
        end = start + chunk_size
        
        # If we're not at the end of the text, try to break at a sentence or word boundary
        if end < text_len:
            # Look for sentence boundaries (., !, ?)
            for sep in ['. ', '! ', '? ', '\n\n', '\n', ' ']:
                sep_pos = text.rfind(sep, start + chunk_size - 100, end)
                if sep_pos != -1:
                    end = sep_pos + len(sep)
                    break
        
        chunks.append(text[start:end])
        start = end - chunk_overlap
    
    return chunks

# Setup directories and URLs
sources_dir = sys.argv[1] if len(sys.argv) > 1 else "sources"
os.makedirs("data", exist_ok=True)
TIKA_URL = os.getenv("TIKA_URL", "http://tika:9998")
OLLAMA_URL = os.getenv("OLLAMA_URL", "http://ollama:11434")

def get_embedding(text):
    """Get embedding from Ollama using nomic-embed-text model"""
    response = requests.post(f"{OLLAMA_URL}/api/embed", json={
        "model": "nomic-embed-text",
        "input": text
    })
    if response.status_code != 200:
        raise Exception(f"Failed to get embedding: {response.text}")
    embedding = response.json()["embeddings"][0]
    print(f"Embedding dimension during ingestion: {len(embedding)}")
    return embedding

texts, docs = [], []

# Process sources recursively
sources_count = 0
for root, dirs, files in os.walk(sources_dir):
    for fname in files:
        # Skip hidden files and common non-document files
        if fname.startswith('.') or fname.endswith(('.pyc', '.faiss', '.pkl', '.jsonl')):
            continue
        
        full_path = os.path.join(root, fname)
        # Create relative path for display
        rel_path = os.path.relpath(full_path, sources_dir)
        
        # Handle different file types
        try:
            if fname.endswith(".txt"):
                # Handle plain text files directly for efficiency
                with open(full_path, "r", encoding="utf-8") as f:
                    raw_text = f.read().strip()
            else:
                # Let Tika handle all other document types
                with open(full_path, "rb") as f:
                    resp = requests.put(f"{TIKA_URL}/tika", headers={"Accept": "text/plain"}, data=f)
                    if resp.status_code == 200:
                        raw_text = resp.text.strip()
                    else:
                        print(f"Warning: Tika couldn't process {rel_path} (status: {resp.status_code})")
                        continue
        except Exception as e:
            print(f"Error processing {rel_path}: {e}")
            continue

        # Split into manageable chunks
        chunks = split_text(raw_text)

        for chunk in chunks:
            chunk = chunk.strip()
            if len(chunk) > 100:  # Ignore very small/non-informative chunks
                texts.append(chunk)
                docs.append({"filename": rel_path, "text": chunk})
        
        sources_count += 1

# Generate embeddings
print("Generating embeddings...")
embeddings = []
for i, text in enumerate(texts):
    if i % 10 == 0:
        print(f"Processing {i}/{len(texts)} embeddings...")
    embedding = get_embedding(text)
    embeddings.append(embedding)

# Build FAISS index
if embeddings:
    dimension = len(embeddings[0])
    print(f"Building FAISS index with dimension: {dimension}")
    index = faiss.IndexFlatL2(dimension)
    # Convert embeddings list to the format FAISS expects
    vectors = [[float(val) for val in emb] for emb in embeddings]
    index.add(vectors)
else:
    raise Exception("No embeddings generated")

# Save FAISS index and metadata
faiss.write_index(index, "data/index.faiss")

with open("data/metadata.jsonl", "w") as f:
    for doc in docs:
        f.write(json.dumps(doc) + "\n")

print(f"Ingested {len(docs)} chunks from {sources_count} files.")

