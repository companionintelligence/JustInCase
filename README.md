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

### 3. Download models (one-time setup)

```bash
# Make the script executable
chmod +x download-models-local.sh

# Download models to your local machine
./download-models-local.sh
```

This downloads models to `./ollama_models_local` (~1.6GB). These are included in the Docker build.

### 4. Build and start the system

```bash
docker compose build
docker compose up
```

This:
- Builds containers with pre-loaded models
- Launches Apache Tika
- Launches Ollama (with models already included)
- Indexes your PDFs
- Starts the Flask API + web UI on port `8080`

**Note:** Models are baked into the Docker image. No internet connection needed after build!

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

### Model Management

Models are pre-downloaded to your local machine and baked into the Docker image during build.

**Important:** 
- Models are stored in `./ollama_models_local` (git-ignored)
- Models are copied into Docker images during build
- No dynamic downloading - fails fast if models are missing
- ~1.6GB total (1.3GB for llama3.2:1b, 274MB for nomic-embed-text)

### First-time setup

```bash
# Download models locally (one time only)
./download-models-local.sh

# Build Docker images with models included
docker compose build
```

### Rebuilding without re-downloading

Once models are in `./ollama_models_local`, they're reused for all builds:

```bash
# Models are already local, just rebuild
docker compose build
docker compose up
```

### If models are missing

The Docker build will fail with a clear error if models aren't found:
```
ERROR: Ollama models not found! Run ./download-models-local.sh first
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

Models are stored locally in `./ollama_models_local` and copied into Docker images during build.

To check your local models:
```bash
# Check if models exist locally
ls -la ./ollama_models_local/

# Check size
du -sh ./ollama_models_local/

# With containers running, verify models in container
docker compose exec ollama ollama list
```

To completely reset and re-download models:
```bash
# Remove local models
rm -rf ./ollama_models_local/

# Re-download
./download-models-local.sh

# Rebuild Docker images
docker compose build
```

### Troubleshooting

If build fails with "models not found":

1. **Run the download script**: `./download-models-local.sh`
2. **Check models exist**: `ls -la ./ollama_models_local/models/`
3. **Ensure sufficient disk space**: Need ~1.6GB free
4. **Try manual Docker download** if local Ollama isn't working:
   ```bash
   docker run -d --name ollama-temp -v "$(pwd)/ollama_models_local:/root/.ollama" ollama/ollama:0.12.6
   docker exec ollama-temp ollama pull llama3.2:1b
   docker exec ollama-temp ollama pull nomic-embed-text
   docker stop ollama-temp && docker rm ollama-temp
   ```

