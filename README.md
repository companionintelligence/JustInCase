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

### 3. Use your existing local models (NO DOWNLOADS!)

If you already have Ollama installed locally with models:

```bash
# This just creates a mount point - NO DOWNLOADS!
chmod +x prepare-local-models.sh
./prepare-local-models.sh
```

### 4. Start the system

```bash
docker compose up --build
```

This:
- Launches Apache Tika
- Launches Ollama (using YOUR LOCAL models)
- Indexes your PDFs
- Starts the Flask API + web UI on port `8080`

**Note:** This uses your existing ~/.ollama directory. No internet required!

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

## ⚙️ Configuration

### Model Selection

You can customize which models to use by setting environment variables:

```bash
# Use different models
export LLM_MODEL=llama3.2
export EMBEDDING_MODEL=nomic-embed-text

# Download your chosen models
./download-models-local.sh

# Build and run with your models
docker compose build
docker compose up
```

All model references are centralized in `config.py` for easy modification.

## 🔧 Troubleshooting

### Model Management

**NEW APPROACH: Use your existing local Ollama models directly!**

**Important:** 
- Uses YOUR existing ~/.ollama directory (no copying, no downloads!)
- Models are mounted read-only from your local system
- NO INTERNET CONNECTION REQUIRED
- Works with whatever models you already have locally
- ~1.6GB total (varies by model choice)

### Workflow:

1. **Setup** (one time, no downloads):
   ```bash
   ./prepare-local-models.sh
   ```

2. **Run the system**:
   ```bash
   docker compose up
   ```

3. **That's it!** Your local models are used directly.

### If you see "models not found":

Check what models you actually have:
```bash
ollama list
```

Then set the environment variables to match:
```bash
export LLM_MODEL=llama3.2:latest  # or whatever you have
export EMBEDDING_MODEL=nomic-embed-text:latest
docker compose up
```

### If models are missing:

The system will fail fast with clear error messages if models aren't found.
No automatic downloads, no fallbacks.

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

Models are stored in `./ollama_models` (copied from `~/.ollama`).

To check your models:
```bash
# Check local Ollama models
ollama list

# Check prepared models
ls -la ./ollama_models/

# Check size
du -sh ./ollama_models/

# With containers running, verify models
docker compose exec ollama ollama list
```

### Troubleshooting

If models are missing:

1. **Run prepare script** (it handles everything):
   ```bash
   # This will fetch models for you
   ./prepare-models.sh
   ```

2. **Check for errors** - the script will tell you exactly what's missing

3. **Alternative: Use local Ollama**:
   ```bash
   # If you prefer using local Ollama
   USE_LOCAL_OLLAMA=true ./prepare-models.sh
   ```

The system is designed to fail fast and clearly if models aren't available.
No surprises, no hidden downloads.

