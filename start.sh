#!/bin/bash

echo "Waiting for Tika..."
until curl -s http://tika:9998 >/dev/null; do
  sleep 2
done

echo "Waiting for Ollama..."
until curl -s http://ollama:11434/api/tags >/dev/null; do
  sleep 2
done

echo "make sure llama3.2:1b is up"
curl http://ollama:11434/api/pull -d '{"name": "llama3.2:1b"}'

echo "make sure nomic-embed-text is up"
curl http://ollama:11434/api/pull -d '{"name": "nomic-embed-text"}'

echo "Tika and Ollama are up. Ingesting PDFs..."
python3 ingest.py pdfs

echo "Starting server..."
python3 serve.py
