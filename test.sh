#!/bin/bash

echo "Running Tika test..."
curl -s --fail -T sample.pdf http://localhost:9998/tika > /dev/null && echo "✅ Tika is working" || echo "❌ Tika failed"

echo "Running search test..."
curl -s -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"query": "How do I purify water?"}' | jq .