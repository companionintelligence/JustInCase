# Install Just In Case

Just In Case (JIC) runs on your own computer as a set of Docker containers. It answers questions from a library of emergency documents without needing the internet. This page covers macOS, Windows and Linux.

## What you need

- **Docker with Docker Compose.** Docker Desktop on macOS and Windows, or Docker Engine with the Compose plugin on Linux. See [Get Docker](https://docs.docker.com/get-docker/).
- **Git**, to get the source.
- **About 4 GB** of free disk space for the two model files.
- **Space for the library.** The core library is about 350 MB. The emergency ZIMs add about 20 GB, and the full encyclopedic ZIMs about 240 GB. See [Hardware](1500-hardware.md) for drive sizes.

Models are not downloaded at runtime. You place them in `./gguf_models/` before you start Docker.

## Windows

Run the commands in **WSL 2** (Ubuntu) with Docker Desktop's WSL 2 backend turned on. The helper scripts are bash scripts and do not run in PowerShell.

1. Install [Docker Desktop for Windows](https://docs.docker.com/desktop/setup/install/windows-install/) with the WSL 2 backend.
2. Install WSL 2 and an Ubuntu distribution if you do not have them: `wsl --install` in an administrator PowerShell, then restart.
3. In Docker Desktop, under **Settings → Resources → WSL integration**, turn on your Ubuntu distribution.
4. Open the Ubuntu terminal and continue with [Get the source](#get-the-source) below.

Keep the project inside the Linux file system (for example `~/just-in-case`), not under `/mnt/c`. Docker and the model files are much faster there.

## macOS

1. Install [Docker Desktop for Mac](https://docs.docker.com/desktop/setup/install/mac-install/) and start it.
2. Open Terminal and continue with [Get the source](#get-the-source) below.

## Linux

1. Install [Docker Engine](https://docs.docker.com/engine/install/) and the Compose plugin. Add your user to the `docker` group and log out and back in.
2. Check that Compose works: `docker compose version`.
3. Continue with [Get the source](#get-the-source) below.

## Get the source

```bash
git clone https://github.com/companionintelligence/JustInCase.git
cd JustInCase
```

## 1. Download the models

```bash
./helper-scripts/fetch-models.sh
```

The script uses `wget` or `curl`, whichever is installed. It places two GGUF files in `./gguf_models/`: a language model of about 4.3 GB and an embedding model of about 265 MB.

Check the files are there before you go on:

```bash
ls -lh gguf_models/
```

## 2. Build and start

```bash
docker compose up --build -d
docker compose ps        # the services should show as running
```

The first build compiles everything from source and can take a while. The web UI is at **http://localhost:8080**.

## 3. Load the library

JIC does not include the documents in the image. Download them into the library:

```bash
# Core emergency PDFs (about 350 MB)
./helper-scripts/fetch-source-data.sh --profile core

# Or inside Docker Compose
docker compose --profile fetch run --rm content-fetch
```

Other profiles are `emergency-zims` (about 20 GB), `full-zims` (about 240 GB) and `kalite`. Indexing picks up new files within about 30 seconds.

You can add your own documents by copying them into the library directory, or with:

```bash
docker compose cp my-manual.pdf jic-server:/app/public/sources/100_Survival/
```

## 4. Check it works

1. Open http://localhost:8080 from this machine.
2. Ask a question that the library should answer, for example about water purification.
3. Check that the answer cites a document from the library.

To open it from another device on your network, use `http://<this-computer's-address>:8080`. Only do this on a network you trust, and check the [security posture](../README.md#security-posture) first.

## Stop, start and update

```bash
docker compose down          # stop; keeps the library and index
docker compose up -d         # start again
```

Do not add `-v` to `docker compose down`. That deletes the library and index volumes.

To update the code, pull the latest source and rebuild:

```bash
git pull
docker compose up --build -d
```

## Troubleshooting

- **The page loads but answers are missing or say no model is loaded.** Check `./gguf_models/` has both files, then restart: `docker compose restart`.
- **`fetch-models.sh` fails.** Check your internet connection and run it again.
- **The build fails with out-of-memory errors.** Raise Docker's memory limit in Docker Desktop's settings, or on Linux free up memory and try again.
- **Port 8080 is in use.** Stop the other program, or change the port in `docker-compose.yml`.

For the design and the rest of the documentation, see [`docs/1000-index.md`](1000-index.md).
