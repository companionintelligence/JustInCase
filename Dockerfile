# syntax=docker/dockerfile:1
FROM python:3.11-slim-bookworm

# Install system dependencies including build tools for llama.cpp
RUN apt-get update && apt-get install -y \
    curl \
    git \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy requirements first for better layer caching
COPY requirements.txt .

# Use pip cache mount for faster rebuilds
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install -r requirements.txt

# Copy application code
COPY . .

# Build llama.cpp
RUN chmod +x build-llama-cpp.sh && ./build-llama-cpp.sh

# Create data directory
RUN mkdir -p /app/data

# Expose ports
EXPOSE 8080 11434

ENTRYPOINT ["python3", "start.py"]
