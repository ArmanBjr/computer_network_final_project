#!/bin/bash
# Start FileShareX containers
# Usage: ./scripts/start_containers.sh

set -e

echo "=========================================="
echo "Starting FileShareX Containers"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Check if we're in the right directory
if [ ! -f "docker/compose.yml" ]; then
    echo -e "${RED}Error: Run this script from project root (where docker/compose.yml exists)${NC}"
    exit 1
fi

# Step 1: Stop any existing containers
echo -e "${YELLOW}[1/4] Stopping existing containers...${NC}"
docker compose -f docker/compose.yml down 2>/dev/null || true
echo -e "${GREEN}✓ Containers stopped${NC}"
echo ""

# Step 2: Build images (this will take time if images were deleted)
echo -e "${YELLOW}[2/4] Building Docker images...${NC}"
echo "This may take a few minutes if images were deleted..."
docker compose -f docker/compose.yml build --no-cache
if [ $? -ne 0 ]; then
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Images built${NC}"
echo ""

# Step 3: Start containers
echo -e "${YELLOW}[3/4] Starting containers...${NC}"
docker compose -f docker/compose.yml up -d
if [ $? -ne 0 ]; then
    echo -e "${RED}✗ Failed to start containers${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Containers started${NC}"
echo ""

# Step 4: Wait for services to be ready
echo -e "${YELLOW}[4/4] Waiting for services to be ready...${NC}"
sleep 5

# Check database
echo -n "Checking database... "
for i in {1..30}; do
    if docker exec fsx_db pg_isready -U fsx >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
        break
    fi
    if [ $i -eq 30 ]; then
        echo -e "${RED}✗ Database not ready after 30 seconds${NC}"
        exit 1
    fi
    sleep 1
done

# Check core server
echo -n "Checking core server... "
for i in {1..30}; do
    if docker ps | grep -q "fsx_core.*Up"; then
        echo -e "${GREEN}✓${NC}"
        break
    fi
    if [ $i -eq 30 ]; then
        echo -e "${RED}✗ Core server not ready after 30 seconds${NC}"
        exit 1
    fi
    sleep 1
done

# Check gateway
echo -n "Checking gateway... "
for i in {1..30}; do
    if docker ps | grep -q "fsx_gateway.*Up"; then
        echo -e "${GREEN}✓${NC}"
        break
    fi
    if [ $i -eq 30 ]; then
        echo -e "${RED}✗ Gateway not ready after 30 seconds${NC}"
        exit 1
    fi
    sleep 1
done

echo ""
echo "=========================================="
echo -e "${GREEN}All services are running!${NC}"
echo "=========================================="
echo ""
echo "Services:"
echo "  - Database:    fsx_db (PostgreSQL on port 5432)"
echo "  - Core Server: fsx_core (TCP on port 9000)"
echo "  - Gateway:     fsx_gateway (HTTP on port 8000)"
echo ""
echo "Useful commands:"
echo "  - View logs:    docker logs -f fsx_core"
echo "  - Stop all:     docker compose -f docker/compose.yml down"
echo "  - Restart:      docker compose -f docker/compose.yml restart"
echo ""

