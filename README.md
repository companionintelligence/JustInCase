# 🛟 Survival-RAG: Offline Emergency Knowledge Search

This project creates a self-contained, LLM-powered search engine over a curated set of survival PDFs. It is designed for **offline use**, **durability**, and **ease of deployment** — suitable even for novice users during emergency situations.

---

## 📦 Components

| Component       | Role                                             |
|-----------------|--------------------------------------------------|
| 🧠 `Ollama`       | LLM inference engine (e.g. `llama3`)             |
| 📄 `Apache Tika` | PDF-to-text extractor                            |
| 🔍 `FAISS`        | Vector search over parsed PDF chunks            |
| 🌐 `Flask App`    | Simple API + minimal HTML frontend              |
| 🧪 `test.sh`      | Shell script for validating core functionality  |

---

## 🚀 Quickstart

### 1. Install Docker + Docker Compose

Make sure your system supports Docker. See [https://docs.docker.com/get-docker/](https://docs.docker.com/get-docker/)

### 2. Unzip and enter project directory

```bash
unzip full_survival_stack.zip
cd full_survival_stack
```

### 3. Add your PDFs

Put any `.pdf` files you want indexed into the `pdfs/` folder.

> You can start with the [Survival Data Corpus](https://github.com/PR0M3TH3AN/Survival-Data)

```bash
git clone https://github.com/PR0M3TH3AN/Survival-Data.git
find Survival-Data/HOME -type f -iname "*.pdf" -exec cp {} pdfs/ \;
```

### 4. Start the whole system

```bash
docker compose up --build
```

This:
- Launches Apache Tika
- Launches Ollama
- Indexes your PDFs
- Starts the Flask API + web UI on port `8080`

---

## 🌐 Access

- Web Interface: [http://localhost:8080](http://localhost:8080)
- API: `POST /query`  
  Example:

```bash
curl -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"query": "How do I purify water in the wild?"}'
```

---

## 🧪 Diagnostics

Run basic functionality checks:

```bash
bash test.sh
```

This verifies:
- Tika is working
- Query endpoint returns valid results

---

## 🧰 Files

- `Dockerfile`: builds the Flask vector search app
- `docker-compose.yml`: orchestrates Tika, Ollama, and your app
- `start.sh`: waits for dependencies, then runs `ingest.py` and `serve.py`
- `ingest.py`: extracts text and builds a vector index
- `serve.py`: serves search API and frontend
- `test.sh`: quick validation script
- `frontend.html`: simple browser-based interface

---

## 💡 Customizing the Model

By default, the system queries `llama3` via Ollama.

You can:
- Modify `serve.py` to adjust prompt formatting
- Swap to another model by editing the `"model": "llama3"` key

Make sure the model is pulled into Ollama via:

```bash
ollama run llama3
```

---

## 🆘 Goal

> Provide **durable**, **offline**, **LLM-powered answers** to survival questions, especially in scenarios where the internet is unavailable.

Built with resilience and simplicity in mind.

---

## 📜 License

MIT / Apache 2.0 (use freely, but no warranties).
