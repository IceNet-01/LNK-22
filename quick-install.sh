#!/bin/bash
# MeshNet - Quick Install Script
# One-command install for development environments

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}╔════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   MeshNet - Quick Install              ║${NC}"
echo -e "${BLUE}╔════════════════════════════════════════╗${NC}"
echo ""

# Check prerequisites
echo -e "${BLUE}[1/5]${NC} Checking prerequisites..."
command -v python3 >/dev/null 2>&1 || { echo "Python 3 required but not installed. Aborting."; exit 1; }
command -v node >/dev/null 2>&1 || { echo "Node.js required but not installed. Aborting."; exit 1; }
echo -e "${GREEN}✓${NC} Python 3 and Node.js found"

# Install Python dependencies
echo ""
echo -e "${BLUE}[2/5]${NC} Installing Python dependencies..."
pip3 install --break-system-packages rns lxmf aiohttp aiohttp-cors 2>&1 | grep -v "Requirement already satisfied" || true
echo -e "${GREEN}✓${NC} Python dependencies installed"

# Create config files
echo ""
echo -e "${BLUE}[3/5]${NC} Creating configuration files..."

# Create config.json if it doesn't exist
if [ ! -f config.json ]; then
    cp config.example.json config.json
    echo -e "${GREEN}✓${NC} Created config.json"
else
    echo -e "${YELLOW}!${NC} config.json already exists, skipping"
fi

# Create/overwrite Reticulum config
mkdir -p ~/.reticulum
cat > ~/.reticulum/config << 'EOF'
[reticulum]
  enable_transport = no
  share_instance = yes
  shared_instance_port = 37428
  instance_control_port = 37429

# AutoInterface enabled by default for local network discovery
[[AutoInterface]]
  type = AutoInterface
  enabled = yes

# Additional interfaces (uncomment and configure as needed):

# [[UDP Interface]]
#   type = UDPInterface
#   enabled = no
#   listen_ip = 0.0.0.0
#   listen_port = 4242
#   forward_ip = 255.255.255.255
#   forward_port = 4242

# [[TCP Client]]
#   type = TCPClientInterface
#   enabled = no
#   target_host = hub.reticulum.network
#   target_port = 4965
EOF
echo -e "${GREEN}✓${NC} Created ~/.reticulum/config"

# Install Node dependencies
echo ""
echo -e "${BLUE}[4/5]${NC} Installing Node.js dependencies..."
npm install --silent 2>&1 | grep -v "npm WARN" || true
echo -e "${GREEN}✓${NC} Root package dependencies installed"

cd frontend
npm install --silent 2>&1 | grep -v "npm WARN" || true
echo -e "${GREEN}✓${NC} Frontend dependencies installed"
cd ..

# Create necessary directories
echo ""
echo -e "${BLUE}[5/5]${NC} Creating data directories..."
mkdir -p data logs
echo -e "${GREEN}✓${NC} Directories created"

# Done!
echo ""
echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   Installation Complete! 🎉            ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════╝${NC}"
echo ""
echo -e "To start MeshNet, run:"
echo -e "  ${BLUE}npm start${NC}"
echo ""
echo -e "This will start both backend and frontend in development mode."
echo ""
echo -e "Access the web UI at: ${BLUE}http://localhost:5173${NC}"
echo ""
echo -e "To configure interfaces, edit: ${BLUE}~/.reticulum/config${NC}"
echo ""
