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

'''

from langchain_community.vectorstores import FAISS
from langchain_community.embeddings import HuggingFaceEmbeddings

# Define the path to your FAISS index
index_folder = "data"
index_name = "index"
index_path = os.path.join(index_folder, f"{index_name}.faiss")
pkl_path = os.path.join(index_folder, f"{index_name}.pkl")
os.makedirs(index_folder, exist_ok=True)

# Use your 384-dim model
model_name = "all-MiniLM-L6-v2"
model = SentenceTransformer(model_name)
print("Model embedding dimension:", model.get_sentence_embedding_dimension())

# Initialize HuggingFaceEmbeddings wrapper for LangChain
embeddings = HuggingFaceEmbeddings(model_name=model_name)

# Sample data
texts = ["This is the first document.", "Here is another document.", "Final piece of content."]
metadatas = [{"text": t, "source": f"doc{i+1}.txt"} for i, t in enumerate(texts)]

# Save metadata.jsonl
metadata_path = os.path.join(index_folder, "metadata.jsonl")
with open(metadata_path, "w") as f:
    for metadata in metadatas:
        f.write(json.dumps(metadata) + "\n")

# Build FAISS index with aligned embeddings
db = FAISS.from_texts(texts, embeddings, metadatas=metadatas)
db.save_local(folder_path=index_folder, index_name=index_name)

print("FAISS index and metadata rebuilt successfully.")

'''

