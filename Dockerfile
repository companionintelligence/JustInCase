FROM python:3.11-slim

RUN apt-get update && apt-get install -y \
    curl git \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . /app
WORKDIR /app
ENTRYPOINT ["bash", "start.sh"]