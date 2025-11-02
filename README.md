# 🛟 Just In Case: Offline Emergency Knowledge Search 🛟

## [LIVE WEB DEMO](https://jic.companionintel.com)

### [Learn More CompanionIntelligence.com/JIC](https://companionintelligence.com/JIC)

If the Internet goes dark, you should still be able to quickly find the knowledge that can help you survive and thrive during a crisis. The goal of the JIC project is to deliver a self-contained LLM-powered / AI driven conversational search engine over a curated set of emergency survival pdfs that can be used without the Internet.

This includes survival guides, medical references, even agricultural know-how and engineering resources, as well as broader educational materials like offline Wikipedia, open textbooks, art and humanities. If the Internet goes dark, you should still be able to quickly find the knowledge that can help you survive and thrive during a crisis.


Please feel free to join us. This is a work in progress and we welcome your participation. 

<a target="_blank" href="https://discord.gg/companion"><img src="https://dcbadge.limes.pink/api/server/companion" alt="COMPANION DISCORD" /></a>


Also note this is for research purposes only - do not rely on this for real world use at this time. Use at your own risk.

## The challenge: Conversational Search

Many of us have some kind of plan for a crisis; a flashlight in a drawer, extra food supplies, water, cash, a map of community resources, a muster-point, a group of friends to rely on. We're generally aware of region specific risks, tornados, tsunamis, forest fires - but what's missing is actionable instant information - the stuff that fills in the gaps in the larger plan.

For better or worse we now heavily rely on conversational search tools such as ChatGPT, Claude, Google AI and other online resources. Even for small questions such as "how do you wire up rechargeable batteries to a solar panel?" or "what is the probable cause of a lower right side stomach pain?".

The problem is that these services are often cloud based, and not always at our fingertips. We don't realize that during an emergency, the very thing we rely on may not be there. To get a feel for this try turn off your phone and the Internet for just one day.

The fact is that there's a real difference between the difficulty of trying to read through a book on how to treat a burn during an emergency, versus getting some quick help or counsel, conversationally, right away, or at least getting some quick guidance on where to look for more details. What would you do if the internet went down? Or even just an extended power outage? What is your families plan for region-specific threats such as tornadoes, tsunamis, or forest fires? Many of us have some kind of plan; a flashlight in a drawer, extra food supplies, water, cash, a map of community resources, a muster-point.

The world has changed, we now heavily rely on tools such as ChatGPT, Claude, Google and other online resources. Even for small questions such as "how do you wire up rechargeable batteries to a solar panel?" or "what is the probable cause of a lower right side stomach pain?". The thing most of us rely heavily on information itself, and that information is not always at our fingertips.

Validating a tool like this raises many questions. Who are typical users of the dataset? What are typical scenarios? Can we build a list of typical questions a user may ask of the dataset? Can we have regression tests against the ability of the dataset to resolve the queries? Are there differences in what is needed for short, medium or extended emergencies or extended survival situations? In this ongoing project we'll try to tackle these and improve this over time.

Conversational interfaces can bridge a gap - at the very least providing search engine like guidance on how to get more information quickly.

## Proposed approach:

Overall the goal of the JIC project is to provide offline information:

- a wide variety of documents and reference material
- possibly later also live maps of recent regional data
- interactive intelligence / llms

The novel feature (as mentioned) that seems the most useful for non-technical users is a conversational interface to emergency data.

To tackle that we propose an offline llm driven knowledge corpus, collecting documents, and providing actionable intelligence over them. This is a variation of common corporate RAG style document stores with query engines, but needs to run in an offline context.

## Classical offline knowledge sources

There are many excellent existing efforts to provide offline emergency knowledge - and those are great fuel for the LLM. This includes survival guides, medical references, even agricultural know-how and engineering resources, as well as broader educational materials like offline Wikipedia, open textbooks, art and humanities. There are also many challenges here in terms of deciding what to curate, what kinds of ongoing aggregation are needed (prior to a catastrophe) and how to organize that knowledge statically.

## Design thinking

Validating a tool like this raises many questions. Who are typical users of the dataset? What are typical scenarios? Can we build a list of typical questions a user may ask of the dataset? Can we have regression tests against the ability of the dataset to resolve the queries? Are there differences in what is needed for short, medium or extended emergencies or extended survival situations? Here are a few documents that we used to ground our thinking so far:

[Typical Questions](docs/1100-questions.md)

[Persona](docs/1200-persona.md)

[Categorization](docs/1300-categorization.md)

Note as well that the general topic of ingesting large amounts of data and making that data conversationally accessible (by prompting the llm with appropriate context) is a well known one, and this proof of concept effectively is an implementation of that larger thesis. Here are a few details on lower level technical aspects:

[Data Sources](docs/1400-sources.md)

[Hardware](docs/1500-hardware.md)

[Architecture](docs/1600-architecture.md)

## Screenshot

![screenshot](screenshot.png?raw=true "screenshot")

## 📦 Approach: Components

We've tried a variety of approaches, ollama, python, n8n - our current stack is turning out to be simply C++ with what feels like 'direct to the metal' interactions with the tools we need:

| Component         | Role                                            |
|-------------------|-------------------------------------------------|
| 🧠 `Llama.cpp`    | LLM loader (e.g. `llama3`)                      |
| 📄 `Apache Tika`  | PDF-to-text extractor                           |
| 🔍 `FAISS`        | Vector search over parsed PDF chunks            |
| 🌐 `C++ Server`   | Simple API + minimal HTML frontend              |

Note we may shift a few pieces around here - may move to pgvector for example.

We're thinking of these engines for chewing through the context (the pdfs) - basically presenting each pdf page (generated with Tika) to qwen2.5-vl. Using a smaller model to be (high end) laptop friendly:

https://huggingface.co/ggml-org/Qwen2.5-VL-7B-Instruct-GGUF

This also seems interesting but is not used yet:

https://github.com/deepseek-ai/DeepSeek-OCR/blob/main/DeepSeek_OCR_paper.pdf

## 🚀 Quickstart

### 1. Install Docker + Docker Compose

Make sure your system supports Docker. See [https://docs.docker.com/get-docker/](https://docs.docker.com/get-docker/)

### Build Options

### 1. Add your PDFs

Put any `.pdf` files or text files you want indexed into the `public/sources/` folder. These will make them visible to both the runtime ingestion and to the server so the user can see the documents when they are returned as part of an llm suggested action.

### [Starting Data Sources](https://github.com/companionintelligence/JustInCase/tree/main/public/sources)

> You can start with the [Survival Data Corpus](https://github.com/PR0M3TH3AN/Survival-Data)

> Also see [fetch-source-data.sh](helper-scripts/fetch-source-data.sh) - this is a helpful script to fetch some other sources - however sources may change and things may break - so this may need ongoing work.

```bash
git clone https://github.com/PR0M3TH3AN/Survival-Data.git
find Survival-Data/HOME -type f -iname "*.pdf" -exec cp {} sources/ \;
```

### 2. Prepare models

**Important:** Our setup of Docker will avoid downloading models from the internet. You must prepare them locally first.

1. **Download GGUF files manually** (one time, on any connection):
   ```bash
   # Create directory
   mkdir -p gguf_models
   
   # Run this for download instructions and URLs:
   ./helper-scripts/fetch-models.sh
   
   # The script will show you the exact URLs and wget commands
   # Place downloaded .gguf files in ./gguf_models/
   ```

2. **Load models into Docker**:
   ```bash
   # Download GGUF models
   ./helper-scripts/fetch-models.sh
   
   # Start Docker containers
   docker compose up --build
   ```

For detailed instructions, run: `./helper-scripts/fetch-models.sh`

### 3. Start the system

```bash
docker compose up --build
```

This:
- Launches Apache Tika
- Starts ingestor that will continually indexes your PDFs
- Starts the web UI on port `8080`

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
./helper-scripts/fetch-models.sh

# Build and run with your models
docker compose build
docker compose up
```

