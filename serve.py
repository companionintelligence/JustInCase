import os
import faiss
import json
from flask import Flask, request, jsonify, send_from_directory
from sentence_transformers import SentenceTransformer
import requests

from langchain_community.vectorstores import FAISS
from langchain_community.embeddings import HuggingFaceEmbeddings

# Initialize your embeddings
embeddings = HuggingFaceEmbeddings()

# Define the path to your FAISS index
index_folder = "data"
index_name = "index"
index_path = os.path.join(index_folder, f"{index_name}.faiss")
pkl_path = os.path.join(index_folder, f"{index_name}.pkl")

# Check if both index files exist
if os.path.exists(index_path) and os.path.exists(pkl_path):
    # Load the existing FAISS index
    db = FAISS.load_local(
        folder_path=index_folder,
        embeddings=embeddings,
        index_name=index_name,
        allow_dangerous_deserialization=True
    )
else:
    texts = ["This is the first document.", "Here is another document.", "Final piece of content."]
    metadatas = [{"text": t, "source": f"doc{i+1}.txt"} for i, t in enumerate(texts)]
    with open("data/metadata.jsonl", "w") as f:
        for metadata in metadatas:
            f.write(json.dumps(metadata) + "\n")
    db = FAISS.from_texts(texts, embeddings)
    db.save_local(folder_path=index_folder, index_name=index_name)

if os.path.exists(index_path) and os.path.exists(pkl_path):
    print("folder exists")
else:
    raise FileNotFoundError(
        f"FAISS index files not found in {index_folder}. "
        f"Expected files: {index_name}.faiss and {index_name}.pkl"
    )


app = Flask(__name__)
model = SentenceTransformer("all-MiniLM-L6-v2")
index = faiss.read_index("data/index.faiss")
with open("data/metadata.jsonl") as f:
    docs = [json.loads(line) for line in f]

OLLAMA_URL = os.getenv("OLLAMA_URL", "http://ollama:11434")

@app.route("/query", methods=["POST"])
def query():
    q = request.json.get("query", "")
    qvec = model.encode([q])
    D, I = index.search(qvec, k=5)
    context = "\n\n".join(docs[i]["text"] for i in I[0])

    response = requests.post(f"{OLLAMA_URL}/api/generate", json={
        "model": "llama3",
        "prompt": f"Context: {context}\n\nQuestion: {q}\n\nAnswer:"
    }, timeout=60)
    reply = response.json().get("response", "No response")
    return jsonify({"answer": reply, "matches": [docs[i] for i in I[0]]})

@app.route("/")
def serve_ui():
    return send_from_directory(".", "frontend.html")

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
