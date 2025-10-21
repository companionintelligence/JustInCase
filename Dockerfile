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

ENTRYPOINT ["python3", "start.py"]
