#!/bin/bash
# Fix WSL Docker build issue by moving project to Linux filesystem
# Usage: Run this script from WSL, then cd to ~/filesharex

set -e

echo "=== FileShareX WSL Docker Fix ==="
echo ""
echo "This script helps fix Docker build issues when project is on Windows filesystem (/mnt/e/...)"
echo ""
echo "RECOMMENDED SOLUTION:"
echo "1. Copy project to Linux filesystem:"
echo "   cp -r /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex ~/filesharex"
echo ""
echo "2. Change to Linux filesystem:"
echo "   cd ~/filesharex"
echo ""
echo "3. Build Docker images:"
echo "   docker compose -f docker/compose.yml build"
echo ""
echo "ALTERNATIVE: If you must stay on Windows filesystem, try:"
echo "1. Use .dockerignore (already created)"
echo "2. Build with smaller context:"
echo "   docker compose -f docker/compose.yml build --progress=plain core"
echo ""
echo "For more info, see: https://github.com/microsoft/WSL/issues/4193"

