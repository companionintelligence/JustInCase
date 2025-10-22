# syntax=docker/dockerfile:1
FROM python:3.11-slim-bookworm

WORKDIR /app

# Copy requirements first for better layer caching
COPY requirements.txt .

# Use pip cache mount for faster rebuilds
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install -r requirements.txt

# Copy application code
COPY . .

# Create necessary directories
RUN mkdir -p /app/data /app/sources

# List contents to verify copy
RUN echo "Contents of /app:" && ls -la /app/ && \
    echo "Contents of /app/sources:" && ls -la /app/sources/ || echo "Sources directory is empty or missing"

# Expose ports
EXPOSE 8080 11434

ENTRYPOINT ["python3", "start.py"]
