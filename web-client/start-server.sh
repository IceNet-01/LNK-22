#!/bin/bash

# LNK-22 Enhanced Web Client - Server Startup Script

echo "========================================"
echo "  LNK-22 Mesh Network Web Client"
echo "========================================"
echo ""

# Check for Python
if command -v python3 &> /dev/null; then
    PORT=3000

    echo "Starting web server on port $PORT..."
    echo ""
    echo "📡 Access the interface at:"
    echo "   http://localhost:$PORT/"
    echo ""
    echo "💡 Usage:"
    echo "   1. Open the URL above in Chrome/Edge/Opera"
    echo "   2. Click 'Connect Device' and select your LNK-22"
    echo "   3. Monitor your mesh network in real-time!"
    echo ""
    echo "🛑 Press Ctrl+C to stop the server"
    echo ""
    echo "========================================"
    echo ""

    # Start Python HTTP server
    python3 -m http.server $PORT

else
    echo "❌ Error: Python 3 is not installed"
    echo ""
    echo "Install Python 3:"
    echo "  Ubuntu/Debian: sudo apt install python3"
    echo "  macOS: brew install python3"
    echo ""
    exit 1
fi
