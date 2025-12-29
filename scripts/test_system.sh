#!/bin/bash
# FileShareX System Test Script
# Tests all components: Gateway, Core, Database

set -e

echo "=========================================="
echo "FileShareX System Test"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if Docker containers are running
echo -e "${YELLOW}[1/5] Checking Docker containers...${NC}"
if ! docker ps | grep -q fsx_core; then
    echo -e "${RED}✗ Core container is not running${NC}"
    echo "  Run: docker compose -f docker/compose.yml up -d"
    exit 1
fi
if ! docker ps | grep -q fsx_gateway; then
    echo -e "${RED}✗ Gateway container is not running${NC}"
    exit 1
fi
if ! docker ps | grep -q fsx_db; then
    echo -e "${RED}✗ Database container is not running${NC}"
    exit 1
fi
echo -e "${GREEN}✓ All containers running${NC}"
echo ""

# Test Database connection
echo -e "${YELLOW}[2/5] Testing Database connection...${NC}"
if docker exec fsx_db psql -U fsx -d fsx -c "SELECT 1;" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Database connection OK${NC}"
    USER_COUNT=$(docker exec fsx_db psql -U fsx -d fsx -t -c "SELECT COUNT(*) FROM users;" 2>/dev/null | tr -d ' ')
    echo "  Users table exists, row count: ${USER_COUNT}"
else
    echo -e "${RED}✗ Database connection failed${NC}"
    exit 1
fi
echo ""

# Test Core TCP server (port 9000)
echo -e "${YELLOW}[3/5] Testing Core TCP server (port 9000)...${NC}"
if timeout 2 bash -c "echo > /dev/tcp/127.0.0.1/9000" 2>/dev/null; then
    echo -e "${GREEN}✓ Core TCP port 9000 is listening${NC}"
else
    echo -e "${RED}✗ Core TCP port 9000 is not accessible${NC}"
    echo "  Check: docker logs fsx_core"
fi
echo ""

# Test Gateway HTTP (port 8000)
echo -e "${YELLOW}[4/5] Testing Gateway HTTP (port 8000)...${NC}"
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8000/ || echo "000")
if [ "$HTTP_CODE" = "200" ]; then
    echo -e "${GREEN}✓ Gateway HTTP is responding (code: ${HTTP_CODE})${NC}"
    echo "  Dashboard: http://localhost:8000"
else
    echo -e "${RED}✗ Gateway HTTP failed (code: ${HTTP_CODE})${NC}"
    echo "  Check: docker logs fsx_gateway"
fi
echo ""

# Test Core Admin port (port 9100)
echo -e "${YELLOW}[5/5] Testing Core Admin port (port 9100)...${NC}"
if timeout 2 bash -c "echo > /dev/tcp/127.0.0.1/9100" 2>/dev/null; then
    echo -e "${GREEN}✓ Core Admin port 9100 is listening${NC}"
else
    echo -e "${YELLOW}⚠ Core Admin port 9100 not accessible (may not be implemented yet)${NC}"
fi
echo ""

echo "=========================================="
echo -e "${GREEN}System Test Complete!${NC}"
echo "=========================================="
echo ""

# Get WSL IP for Windows access
WSL_IP=$(hostname -I 2>/dev/null | awk '{print $1}' || echo "")
if [ -n "$WSL_IP" ]; then
    echo -e "${YELLOW}⚠ WSL Port Access:${NC}"
    echo "  From Windows browser, use WSL IP instead of localhost:"
    echo "  Gateway:  http://${WSL_IP}:8000"
    echo "  Core TCP: ${WSL_IP}:9000"
    echo "  Admin:    ${WSL_IP}:9100"
    echo ""
fi

echo "Next steps:"
echo "  1. Open dashboard:"
if [ -n "$WSL_IP" ]; then
    echo "     From Windows: http://${WSL_IP}:8000"
    echo "     From WSL:     http://localhost:8000"
else
    echo "     http://localhost:8000"
fi
echo "  2. Test client connection:"
echo "     cd client && cmake -S . -B build && cmake --build build"
echo "     ./build/fsx_client test_client 127.0.0.1 9000"
echo "  3. View logs:"
echo "     docker logs -f fsx_core"
echo "     docker logs -f fsx_gateway"
echo ""

