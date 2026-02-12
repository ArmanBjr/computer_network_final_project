#!/bin/bash
# Rebuild Docker containers after code changes
# Usage: ./scripts/rebuild_containers.sh [service]
#   service: core, gateway, or all (default: all)

set -e

SERVICE=${1:-all}

echo "=========================================="
echo "Rebuilding Docker Containers"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if we're in the right directory
if [ ! -f "docker/compose.yml" ]; then
    echo -e "${RED}Error: Run this script from project root${NC}"
    exit 1
fi

cd docker

if [ "$SERVICE" = "all" ] || [ "$SERVICE" = "core" ]; then
    echo -e "${YELLOW}Stopping core container...${NC}"
    docker compose -f compose.yml stop core 2>/dev/null || true
    
    echo -e "${YELLOW}Rebuilding core container (this may take a few minutes)...${NC}"
    docker compose -f compose.yml build --no-cache core
    
    echo -e "${GREEN}✓ Core container rebuilt${NC}"
    echo ""
fi

if [ "$SERVICE" = "all" ] || [ "$SERVICE" = "gateway" ]; then
    echo -e "${YELLOW}Stopping gateway container...${NC}"
    docker compose -f compose.yml stop gateway 2>/dev/null || true
    
    echo -e "${YELLOW}Rebuilding gateway container...${NC}"
    docker compose -f compose.yml build --no-cache gateway
    
    echo -e "${GREEN}✓ Gateway container rebuilt${NC}"
    echo ""
fi

echo -e "${YELLOW}Starting services...${NC}"
docker compose -f compose.yml up -d

echo ""
echo -e "${GREEN}✓ Containers rebuilt and started${NC}"
echo ""
echo "Checking container status:"
docker compose -f compose.yml ps

cd ..
