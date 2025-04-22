import os
import faiss
import json
import traceback
from flask import Flask, request, jsonify, send_from_directory
from sentence_transformers import SentenceTransformer
import requests

from langchain_community.vectorstores import FAISS
from langchain_community.embeddings import HuggingFaceEmbeddings

app = Flask(__name__)
model = SentenceTransformer("all-MiniLM-L6-v2")
index = faiss.read_index("data/index.faiss")
with open("data/metadata.jsonl") as f:
    docs = [json.loads(line) for line in f]

OLLAMA_URL = os.getenv("OLLAMA_URL", "http://ollama:11434")

@app.route("/query", methods=["POST"])
def query():
    print("Query Handler Started")
    try:
        q = request.json.get("query", "").strip()
        print(f"Received query: '{q}'")

        if not q:
            print("Empty query received.")
            return jsonify({"error": "Empty query"}), 400

        qvec = model.encode([q])
        print(f"Encoded query vector shape: {qvec.shape}, Expected FAISS index dimension: {index.d}")

        if qvec.shape[1] != index.d:
            error_msg = f"Dimension mismatch: query vector shape {qvec.shape}, index.d {index.d}"
            print(error_msg)
            return jsonify({"error": error_msg}), 500

        D, I = index.search(qvec, k=5)
        print(f"FAISS search results D: {D}, I: {I}")

        context = "\n\n".join(docs[i]["text"] for i in I[0])
        print(f"Generated context: {context}")

        response = requests.post(f"{OLLAMA_URL}/api/generate", json={
            "model": "llama3",
            "stream": False,
            "prompt": f"Context: {context}\n\nQuestion: {q}\n\nAnswer:"
        }, timeout=60)

        print(f"LLM raw response status: {response.status_code}, text: {response.text}")

        if response.status_code != 200:
            return jsonify({"error": "LLM server error", "details": response.text}), 500

        reply = response.json().get("response", "No response")
        result = {"answer": reply, "matches": [docs[i] for i in I[0]]}
        print(f"Final JSON response: {json.dumps(result, indent=2)}")
        return jsonify(result)

    except Exception as e:
        print("Exception occurred during query handling:")
        traceback.print_exc()
        return jsonify({"error": str(e), "traceback": traceback.format_exc()}), 500



@app.route("/")
def serve_ui():
    return send_from_directory(".", "frontend.html")

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
