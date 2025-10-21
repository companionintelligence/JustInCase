import os
import faiss
import json
import traceback
from flask import Flask, request, jsonify, send_from_directory
import requests

app = Flask(__name__)

OLLAMA_URL = os.getenv("OLLAMA_URL", "http://ollama:11434")

def get_embedding(text):
    """Get embedding from Ollama using nomic-embed-text model"""
    response = requests.post(f"{OLLAMA_URL}/api/embed", json={
        "model": "nomic-embed-text",
        "input": text
    })
    if response.status_code != 200:
        raise Exception(f"Failed to get embedding: {response.text}")
    return response.json()["embeddings"][0]

# Load FAISS index and metadata
index = faiss.read_index("data/index.faiss")
with open("data/metadata.jsonl") as f:
    docs = [json.loads(line) for line in f]

@app.route("/query", methods=["POST"])
def query():
    print("Query Handler Started")
    try:
        q = request.json.get("query", "").strip()
        print(f"Received query: '{q}'")

        if not q:
            print("Empty query received.")
            return jsonify({"error": "Empty query"}), 400

        # Encode query using Ollama
        embedding = get_embedding(q)
        # Convert to 2D array format that FAISS expects (1 query, n dimensions)
        qvec = [[embedding[i] for i in range(len(embedding))]]
        print(f"Encoded query vector length: {len(embedding)}, Expected FAISS index dimension: {index.d}")

        if len(embedding) != index.d:
            error_msg = f"Dimension mismatch: query vector length {len(embedding)}, index.d {index.d}"
            print(error_msg)
            return jsonify({"error": error_msg}), 500

        # Perform FAISS search
        D, I = index.search(qvec, k=5)
        print(f"FAISS search results D: {D}, I: {I}")

        # Filter valid indices and limit context chunks
        valid_indices = [i for i in I[0] if i != -1]
        max_chunks = 3
        selected_chunks = valid_indices[:max_chunks]

        if not selected_chunks:
            print("No relevant documents found.")
            return jsonify({"error": "No relevant documents found."}), 404

        # Build concise context
        context = "\n\n".join(docs[i]["text"] for i in selected_chunks)
        print(f"Generated context (truncated): {context[:500]}...")

        # Make request to Ollama (non-streaming)
        response = requests.post(f"{OLLAMA_URL}/api/generate", json={
            "model": "llama3.2:1b",
            "stream": False,
            "prompt": f"Context: {context}\n\nQuestion: {q}\n\nAnswer:"
        }, timeout=60)

        print(f"LLM raw response status: {response.status_code}, text (truncated): {response.text[:500]}")

        if response.status_code != 200:
            return jsonify({"error": "LLM server error", "details": response.text}), 500

        reply = response.json().get("response", "No response")
        result = {"answer": reply, "matches": [docs[i] for i in selected_chunks]}
        print(f"Final JSON response: {json.dumps(result, indent=2)[:500]}...")

        return jsonify(result)

    except Exception as e:
        print("Exception occurred during query handling:")
        traceback.print_exc()
        return jsonify({"error": str(e), "traceback": traceback.format_exc()}), 500

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

