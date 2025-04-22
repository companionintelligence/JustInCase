import os
import sys
import json
import faiss
import requests
from sentence_transformers import SentenceTransformer

model = SentenceTransformer("all-MiniLM-L6-v2")
pdf_dir = sys.argv[1]
os.makedirs("data", exist_ok=True)

TIKA_URL = os.getenv("TIKA_URL", "http://tika:9998")

texts, docs = [], []
for fname in os.listdir(pdf_dir):
    if not fname.endswith(".pdf"): continue
    with open(os.path.join(pdf_dir, fname), "rb") as f:
        resp = requests.put(f"{TIKA_URL}/tika", data=f)
        text = resp.text
    chunks = text.split("\n\n")
    for chunk in chunks:
        chunk = chunk.strip()
        if len(chunk) > 100:
            texts.append(chunk)
            docs.append({"filename": fname, "text": chunk})

vectors = model.encode(texts, show_progress_bar=True)
index = faiss.IndexFlatL2(vectors.shape[1])
index.add(vectors)

faiss.write_index(index, "data/index.faiss")
with open("data/metadata.jsonl", "w") as f:
    for doc in docs:
        f.write(json.dumps(doc) + "\n")