#!/bin/bash
# Setup script for MeshNet development environment

set -e

echo "=========================================="
echo "MeshNet - Setup Script"
echo "=========================================="
echo

# Check Python
echo "Checking Python..."
if ! command -v python3 &> /dev/null; then
    echo "❌ Python 3 is not installed"
    exit 1
fi
echo "✓ Python 3 found: $(python3 --version)"

# Check Node.js
echo "Checking Node.js..."
if ! command -v node &> /dev/null; then
    echo "❌ Node.js is not installed"
    echo "Please install Node.js 18+ from https://nodejs.org/"
    exit 1
fi
echo "✓ Node.js found: $(node --version)"

# Install Python dependencies
echo
echo "Installing Python dependencies..."
pip3 install -r requirements.txt
echo "✓ Python dependencies installed"

# Install frontend dependencies
echo
echo "Installing frontend dependencies..."
cd frontend
npm install
echo "✓ Frontend dependencies installed"
cd ..

# Create config if it doesn't exist
if [ ! -f config.json ]; then
    echo
    echo "Creating config.json from example..."
    cp config.example.json config.json
    echo "✓ config.json created"
fi

# Create necessary directories
mkdir -p data logs

echo
echo "=========================================="
echo "✓ Setup complete!"
echo "=========================================="
echo
echo "To run in development mode:"
echo "  python3 meshnet.py --dev"
echo
echo "To build frontend for production:"
echo "  cd frontend && npm run build"
echo
echo "To install as systemd service:"
echo "  sudo python3 install_service.py"
echo
