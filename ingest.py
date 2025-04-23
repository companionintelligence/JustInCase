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
pdf_dir = sys.argv[1]
os.makedirs("data", exist_ok=True)
TIKA_URL = os.getenv("TIKA_URL", "http://tika:9998")

# Initialize text splitter
splitter = RecursiveCharacterTextSplitter(
    chunk_size=500,     # Approx 500 characters per chunk
    chunk_overlap=50    # Slight overlap for better context continuity
)

texts, docs = [], []

# Process PDFs
for fname in os.listdir(pdf_dir):
    if not fname.endswith(".pdf"):
        continue
    
    with open(os.path.join(pdf_dir, fname), "rb") as f:
        resp = requests.put(f"{TIKA_URL}/tika", headers={"Accept": "text/plain"}, data=f)
        raw_text = resp.text.strip()

    # Split into manageable chunks
    chunks = splitter.split_text(raw_text)

    for chunk in chunks:
        chunk = chunk.strip()
        if len(chunk) > 100:  # Ignore very small/non-informative chunks
            texts.append(chunk)
            docs.append({"filename": fname, "text": chunk})

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

print(f"Ingested {len(docs)} chunks from {len(os.listdir(pdf_dir))} PDFs.")

