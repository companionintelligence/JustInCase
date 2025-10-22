# Central configuration for the JIC system
import os

# Model configuration
LLM_MODEL = os.getenv("LLM_MODEL", "llama3.2")
EMBEDDING_MODEL = os.getenv("EMBEDDING_MODEL", "nomic-embed-text")

# Service URLs
TIKA_URL = os.getenv("TIKA_URL", "http://tika:9998")
OLLAMA_URL = os.getenv("OLLAMA_URL", "http://ollama:11434")

# Model preparation settings
USE_LOCAL_OLLAMA = os.getenv("USE_LOCAL_OLLAMA", "false").lower() == "true"

# Other settings
CHUNK_SIZE = 500
CHUNK_OVERLAP = 50
MAX_CONTEXT_CHUNKS = 3
SEARCH_TOP_K = 5
