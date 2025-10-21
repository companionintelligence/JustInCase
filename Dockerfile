FROM python:3.11-slim-bookworm@sha256:8c1036ec919826052306dfb5286e4753ffd9d5f4c24a3e6a2d9c05f1e5a8c102

# Install only curl with no recommended packages to avoid X11 dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy requirements first for better layer caching
COPY requirements.txt .

# Use pip cache mount for faster rebuilds
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install --no-cache-dir -r requirements.txt

# Copy application code
COPY . .

ENTRYPOINT ["bash", "start.sh"]
