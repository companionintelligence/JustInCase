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

