#!/bin/bash

echo "Caching Tika image locally..."

# Build the Tika image with our Dockerfile (which just caches it)
docker build -f Dockerfile.tika -t jic-tika:cached .

if [ $? -eq 0 ]; then
    echo "Tika image cached successfully as 'jic-tika:cached'"
    echo "The image will now be used locally without pulling from Docker Hub"
else
    echo "Failed to cache Tika image"
    exit 1
fi
