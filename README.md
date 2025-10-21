# 🛟 Just In Case: Offline Emergency Knowledge Search

The goal of the JIC project is to prototype a self-contained LLM-powered / AI driven conversational search engine over a curated set of emergency survival pdfs that can be used without the Internet. This is for research purposes only - do not rely on this for real world use at this time.

This includes survival guides, medical references, even agricultural know-how and engineering resources, as well as broader educational materials like offline Wikipedia, open textbooks, art and humanities. If the Internet goes dark, you should still be able to quickly find the knowledge that can help you survive and thrive during a crisis.

## The Challenge

What would you do if the internet went down? Or even just an extended power outage? What is your families plan for region-specific threats such as tornadoes, tsunamis, or forest fires? Many of us have some kind of plan; a flashlight in a drawer, extra food supplies, water, cash, a map of community resources, a muster-point.

The world has changed, we now heavily rely on tools such as ChatGPT, Claude, Google and other online resources. Even for small questions such as "how do you wire up rechargeable batteries to a solar panel?" or "what is the probable cause of a lower right side stomach pain?". The thing most of us rely heavily on information itself, and that information is not always at our fingertips.

Validating a tool like this raises many questions. Who are typical users of the dataset? What are typical scenarios? Can we build a list of typical questions a user may ask of the dataset? Can we have regression tests against the ability of the dataset to resolve the queries? Are there differences in what is needed for short, medium or extended emergencies or extended survival situations? In this ongoing project we'll try to tackle these and improve this over time.

Note as well that the general topic of ingesting large amounts of data and making that data conversationally accessible (by prompting the llm with appropriate context) is a well known one, and this proof of concept effectively is an implementation of that larger thesis.

Please see the [docs](docs/index.md) for more details on typical questions, persona, categories, hardware.

This is a work in progress and we welcome your participation. Please join us at https://discord.gg/7k8eqhMJWc .

![screenshot](screenshot.png?raw=true "screenshot")

## 📦 Components

| Component         | Role                                            |
|-------------------|-------------------------------------------------|
| 🧠 `Ollama`       | LLM inference engine (e.g. `llama3`)            |
| 📄 `Apache Tika`  | PDF-to-text extractor                           |
| 🔍 `FAISS`        | Vector search over parsed PDF chunks            |
| 🌐 `Flask App`    | Simple API + minimal HTML frontend              |

---

## 🚀 Quickstart

### 1. Install Docker + Docker Compose

Make sure your system supports Docker. See [https://docs.docker.com/get-docker/](https://docs.docker.com/get-docker/)

### 2. Add your PDFs

Put any `.pdf` files or text files you want indexed into the `sources/` folder.

> You can start with the [Survival Data Corpus](https://github.com/PR0M3TH3AN/Survival-Data)

> Also see [fetch-source-data.sh](fetch-source-data.sh)

```bash
git clone https://github.com/PR0M3TH3AN/Survival-Data.git
find Survival-Data/HOME -type f -iname "*.pdf" -exec cp {} sources/ \;
```

### 3. Start the whole system

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

## 🔧 Troubleshooting

### Manually pulling models

If the automatic model pulling fails or times out, you can manually pull the required models:

```bash
# Using docker compose (recommended)
docker compose exec ollama ollama pull llama3.2:1b
docker compose exec ollama ollama pull nomic-embed-text

# Environment variables to control model handling:

# Skip automatic model pulling on startup
SKIP_MODEL_PULL=true docker compose up --build

# Skip all model checks (use if you're sure models are present)
SKIP_MODEL_CHECK=true docker compose up --build
```

### Container naming

Note that when using `docker compose`, container names are prefixed with your project directory name. To find the exact container name:

```bash
docker ps -a | grep ollama
```

### Model dimension mismatch

If you encounter "dimension mismatch" errors, this usually means the index was built with a different embedding model. Remove the existing index and rebuild:

```bash
rm -rf data/
docker compose restart survival-rag
```

### Understanding model storage

**Important:** Ollama models are stored inside the Docker container, not on your host machine directly. However, they ARE persisted in a Docker volume called `ollama_data`.

- Models location in container: `/root/.ollama/models`
- Docker volume on host: `ollama_data` (managed by Docker)
- Volume persists even when containers are stopped/removed

To verify the volume exists and check its size:
```bash
# List Docker volumes
docker volume ls | grep ollama

# Inspect the volume
docker volume inspect ollama_data

# Check volume size (approximate)
docker system df -v | grep ollama_data
```

If you need to completely reset and re-download models:
```bash
# Stop containers
docker compose down

# Remove the volume (this deletes all downloaded models!)
docker volume rm ollama_data

# Start fresh
docker compose up --build
```

### Troubleshooting model persistence

If models seem to disappear between restarts:

1. **Check volume mounting**: Ensure docker-compose.yml has the volume properly defined
2. **Don't use `docker compose down -v`**: The `-v` flag removes volumes!
3. **Check disk space**: Ensure you have enough space for models (llama3.2:1b is ~1GB, nomic-embed-text is ~275MB)

