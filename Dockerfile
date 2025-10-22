# syntax=docker/dockerfile:1
FROM python:3.11-slim-bookworm

WORKDIR /app

# Install system dependencies for llama-cpp-python
RUN apt-get update && apt-get install -y \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements first for better layer caching
COPY requirements.txt .

# Create pip cache directory
RUN mkdir -p /root/.cache/pip

# Install llama-cpp-python using pre-built wheels
# This avoids compilation issues
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install --cache-dir=/root/.cache/pip --upgrade pip wheel setuptools && \
    pip install --cache-dir=/root/.cache/pip \
    --extra-index-url https://abetlen.github.io/llama-cpp-python/whl/cpu \
    llama-cpp-python

# Install other requirements
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install --cache-dir=/root/.cache/pip -r requirements.txt

# Copy application code
COPY . .

# Create necessary directories
RUN mkdir -p /app/data /app/sources

# List contents to verify copy
RUN echo "Contents of /app:" && ls -la /app/ && \
    echo "Contents of /app/sources:" && ls -la /app/sources/ || echo "Sources directory is empty or missing"

# Expose port
EXPOSE 8080

ENTRYPOINT ["python3", "start.py"]
