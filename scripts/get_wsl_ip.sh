#!/bin/bash
# Get WSL IP address for accessing services from Windows

echo "=========================================="
echo "WSL Network Information"
echo "=========================================="
echo ""

# Method 1: hostname -I
WSL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
if [ -n "$WSL_IP" ]; then
    echo "WSL IP (hostname): $WSL_IP"
fi

# Method 2: ip addr
WSL_IP2=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
if [ -n "$WSL_IP2" ]; then
    echo "WSL IP (eth0):     $WSL_IP2"
fi

# Method 3: ip route
WSL_IP3=$(ip route get 1.1.1.1 2>/dev/null | awk '{print $7}' | head -1)
if [ -n "$WSL_IP3" ]; then
    echo "WSL IP (route):    $WSL_IP3"
fi

echo ""
echo "=========================================="
echo "Access URLs from Windows:"
echo "=========================================="
if [ -n "$WSL_IP" ]; then
    echo "Gateway:  http://${WSL_IP}:8000"
    echo "Core TCP: ${WSL_IP}:9000"
    echo "Admin:    ${WSL_IP}:9100"
elif [ -n "$WSL_IP2" ]; then
    echo "Gateway:  http://${WSL_IP2}:8000"
    echo "Core TCP: ${WSL_IP2}:9000"
    echo "Admin:    ${WSL_IP2}:9100"
else
    echo "Could not determine WSL IP"
    echo "Try: hostname -I"
fi
echo ""

