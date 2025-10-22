# 🛟 Just In Case: Offline Emergency Knowledge Search

The goal of the JIC project is to prototype a lightweight, self-contained LLM-powered / AI driven conversational search engine over a curated set of emergency survival pdfs that can be used without the Internet. This is for research purposes only - do not rely on this for real world use at this time.

This includes survival guides, medical references, even agricultural know-how and engineering resources, as well as broader educational materials like offline Wikipedia, open textbooks, art and humanities. If the Internet goes dark, you should still be able to quickly find the knowledge that can help you survive and thrive during a crisis.

**Design Goals:**
- 🪶 **Lightweight** - Minimal dependencies and small footprint
- 🔌 **Offline-first** - Works completely without internet
- ⚡ **Fast** - Native C++ implementation for speed
- 📦 **Self-contained** - Everything runs in Docker

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

### 3. Prepare models (REQUIRED - No automatic downloads!)

**Important:** Docker will NEVER download models from the internet. You must prepare them locally first.

1. **Download GGUF files manually** (one time, on any connection):
   ```bash
   # Create directory
   mkdir -p gguf_models
   
   # Run this for download instructions and URLs:
   ./fetch-models.sh
   
   # The script will show you the exact URLs and wget commands
   # Place downloaded .gguf files in ./gguf_models/
   ```

2. **Load models into Docker** (no internet required):
   ```bash
   chmod +x prepare-gguf-models.sh load-gguf-models.sh fetch-models.sh
   
   # Create Modelfiles from your GGUF files
   ./prepare-gguf-models.sh
   
   # Start Docker containers
   docker compose up -d
   
   # Load models into Ollama (uses local files only)
   ./load-gguf-models.sh
   ```

For detailed instructions, run: `./fetch-models.sh`

### 4. Start the system

```bash
docker compose up --build
```

This:
- Launches Apache Tika
- Launches Ollama (with models you loaded from GGUF files)
- Indexes your PDFs
- Starts the Flask API + web UI on port `8080`

**Note:** No internet connection required after models are loaded!

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

## ⚙️ Configuration

### Model Selection

You can customize which models to use by setting environment variables:

```bash
# Use different models
export LLM_MODEL=llama3.2
export EMBEDDING_MODEL=nomic-embed-text

# Download your chosen models
./load-gguf-models.sh

# Build and run with your models
docker compose build
docker compose up
```

All model references are centralized in `config.py` for easy modification.

## 🔧 Troubleshooting

### Model Management

**This system uses LOCAL GGUF files only - No automatic downloads!**

**How it works:**
1. You download GGUF model files manually (once, on any connection)
2. Place them in `./gguf_models/`
3. Run `./prepare-gguf-models.sh` to create Modelfiles
4. Run `./load-gguf-models.sh` to load into Docker Ollama
5. Models persist in Docker volume `ollama_data`

**Benefits:**
- Complete control over model files
- No surprise downloads
- Works offline after initial setup
- Models persist across container restarts

**Required GGUF files:**
- Llama 3.2: ~1.3GB (Q4_K_M quantization)
- Nomic embed: ~274MB (Q4_K_M quantization)

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

