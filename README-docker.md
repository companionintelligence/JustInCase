# Docker Setup for JIC Server

This project uses Docker Compose V2 (integrated `docker compose` command).

## Prerequisites

- Docker Engine with Compose V2 plugin installed
- ARM64/aarch64 architecture (for Apple Silicon Macs, Raspberry Pi 4, etc.)

## Quick Start

1. Cache the Tika image locally (one-time setup):
   ```bash
   ./cache-tika.sh
   ```

2. Place your GGUF models in the `gguf_models/` directory:
   - `Llama-3.2-1B-Instruct-Q4_K_S.gguf`
   - `nomic-embed-text-v1.5.Q4_K_M.gguf`

3. Start the services:
   ```bash
   docker compose up
   ```

4. Access the web interface at http://localhost:8080

## Building and Running

### Build the images:
```bash
docker compose build
```

### Run in the background:
```bash
docker compose up -d
```

### View logs:
```bash
docker compose logs -f
```

### Stop the services:
```bash
docker compose down
```

### Rebuild after code changes:
```bash
docker compose build --no-cache jic
docker compose up
```

## Testing

Once the server is running, test it with:
```bash
./test-server.sh
```

## Troubleshooting

### Check if services are running:
```bash
docker compose ps
```

### Check resource usage:
```bash
docker stats
```

### Access container shell:
```bash
docker compose exec jic /bin/bash
```

### Clean up everything:
```bash
docker compose down -v
docker system prune -a
```
