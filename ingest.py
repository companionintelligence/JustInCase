import os
import sys
import json
import faiss
import requests
from sentence_transformers import SentenceTransformer
from langchain.text_splitter import RecursiveCharacterTextSplitter

# Initialize embedding model
model = SentenceTransformer("all-MiniLM-L6-v2")

# Setup directories and Tika URL
sources_dir = sys.argv[1] if len(sys.argv) > 1 else "sources"
os.makedirs("data", exist_ok=True)
TIKA_URL = os.getenv("TIKA_URL", "http://tika:9998")

# Initialize text splitter
splitter = RecursiveCharacterTextSplitter(
    chunk_size=500,     # Approx 500 characters per chunk
    chunk_overlap=50    # Slight overlap for better context continuity
)

texts, docs = [], []

# Process sources recursively
sources_count = 0
for root, dirs, files in os.walk(sources_dir):
    for fname in files:
        if not (fname.endswith(".pdf") or fname.endswith(".txt")):
            continue
        
        full_path = os.path.join(root, fname)
        # Create relative path for display
        rel_path = os.path.relpath(full_path, sources_dir)
        
        # Handle different file types
        if fname.endswith(".pdf"):
            with open(full_path, "rb") as f:
                resp = requests.put(f"{TIKA_URL}/tika", headers={"Accept": "text/plain"}, data=f)
                raw_text = resp.text.strip()
        else:  # .txt files
            with open(full_path, "r", encoding="utf-8") as f:
                raw_text = f.read().strip()

        # Split into manageable chunks
        chunks = splitter.split_text(raw_text)

        for chunk in chunks:
            chunk = chunk.strip()
            if len(chunk) > 100:  # Ignore very small/non-informative chunks
                texts.append(chunk)
                docs.append({"filename": rel_path, "text": chunk})
        
        sources_count += 1

# Generate embeddings
vectors = model.encode(texts, show_progress_bar=True)

# Build FAISS index
index = faiss.IndexFlatL2(vectors.shape[1])
index.add(vectors)

# Save FAISS index and metadata
faiss.write_index(index, "data/index.faiss")

with open("data/metadata.jsonl", "w") as f:
    for doc in docs:
        f.write(json.dumps(doc) + "\n")

print(f"Ingested {len(docs)} chunks from {sources_count} files.")

