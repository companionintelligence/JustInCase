# Central configuration for the JIC system
import os

# Model configuration
LLM_MODEL = os.getenv("LLM_MODEL", "llama3.2")
EMBEDDING_MODEL = os.getenv("EMBEDDING_MODEL", "nomic-embed-text")

# Service URLs
TIKA_URL = os.getenv("TIKA_URL", "http://tika:9998")
OLLAMA_URL = os.getenv("OLLAMA_URL", "http://localhost:11434")

# Model preparation settings
USE_LOCAL_OLLAMA = os.getenv("USE_LOCAL_OLLAMA", "false").lower() == "true"

# GGUF Model Download URLs
# You can override these with environment variables
LLAMA_GGUF_REPO = os.getenv("LLAMA_GGUF_REPO", "bartowski/Llama-3.2-1B-Instruct-GGUF")
LLAMA_GGUF_FILE = os.getenv("LLAMA_GGUF_FILE", "Llama-3.2-1B-Instruct-Q4_K_S.gguf")
LLAMA_GGUF_URL = f"https://huggingface.co/{LLAMA_GGUF_REPO}/resolve/main/{LLAMA_GGUF_FILE}"

NOMIC_GGUF_REPO = os.getenv("NOMIC_GGUF_REPO", "nomic-ai/nomic-embed-text-v1.5-GGUF")
NOMIC_GGUF_FILE = os.getenv("NOMIC_GGUF_FILE", "nomic-embed-text-v1.5.Q4_K_M.gguf")
NOMIC_GGUF_URL = f"https://huggingface.co/{NOMIC_GGUF_REPO}/resolve/main/{NOMIC_GGUF_FILE}"

# Other settings
CHUNK_SIZE = 500
CHUNK_OVERLAP = 50
MAX_CONTEXT_CHUNKS = 3
SEARCH_TOP_K = 5
