#!/bin/bash

# Test script for JIC server

echo "Testing JIC server..."

# Test 1: Check if server is running
echo -n "1. Checking server status... "
STATUS=$(curl -s http://localhost:8080/status)
if [ $? -eq 0 ]; then
    echo "OK"
    echo "   Status: $STATUS"
else
    echo "FAILED - Server not responding"
    exit 1
fi

# Test 2: Test static file serving
echo -n "2. Testing static file serving... "
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/)
if [ "$RESPONSE" = "200" ]; then
    echo "OK"
else
    echo "FAILED - Got HTTP $RESPONSE"
fi

# Test 3: Test query endpoint
echo -n "3. Testing query endpoint... "
QUERY_RESPONSE=$(curl -s -X POST http://localhost:8080/query \
    -H "Content-Type: application/json" \
    -d '{"query":"What is water purification?"}')
if [ $? -eq 0 ]; then
    echo "OK"
    echo "   Response: $(echo $QUERY_RESPONSE | jq -r '.answer' 2>/dev/null | head -c 100)..."
else
    echo "FAILED"
fi

echo "Tests complete!"
