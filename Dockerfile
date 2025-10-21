# syntax=docker/dockerfile:1
FROM python:3.11-slim

# Install only curl with no recommended packages to avoid X11 dependencies
# Use cache mount for apt to avoid re-downloading packages
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy requirements first for better layer caching
COPY requirements.txt .

# Use pip cache mount for faster rebuilds
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install -r requirements.txt

# Copy application code
COPY . .

ENTRYPOINT ["bash", "start.sh"]
