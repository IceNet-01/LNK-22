#!/bin/bash
# MeshNet - All-in-One Installer
# This script installs all dependencies and sets up MeshNet

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}"
echo "=========================================="
echo "   MeshNet - Reticulum Network System"
echo "        All-in-One Installer"
echo "=========================================="
echo -e "${NC}"

# Check if running as root (we don't want that for user install)
if [ "$EUID" -eq 0 ]; then
    echo -e "${YELLOW}⚠️  Please run this script as a regular user (not root/sudo)${NC}"
    echo "The script will ask for sudo password when needed."
    exit 1
fi

# Function to print status messages
print_status() {
    echo -e "${BLUE}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

# Detect OS
print_status "Detecting operating system..."
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
    print_success "Linux detected"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
    print_success "macOS detected"
else
    OS="other"
    print_warning "Unknown OS: $OSTYPE"
fi

# Check for required commands
print_status "Checking for required software..."

# Check Python
if command -v python3 &> /dev/null; then
    PYTHON_VERSION=$(python3 --version | cut -d' ' -f2)
    print_success "Python 3 found: $PYTHON_VERSION"
else
    print_error "Python 3 is not installed!"
    echo ""
    echo "Please install Python 3.8 or higher:"
    if [[ "$OS" == "linux" ]]; then
        echo "  Ubuntu/Debian: sudo apt install python3 python3-pip"
        echo "  Fedora: sudo dnf install python3 python3-pip"
        echo "  Arch: sudo pacman -S python python-pip"
    elif [[ "$OS" == "macos" ]]; then
        echo "  brew install python3"
    fi
    echo "  Or download from: https://www.python.org/downloads/"
    exit 1
fi

# Check pip
if command -v pip3 &> /dev/null; then
    print_success "pip3 found"
else
    print_error "pip3 is not installed!"
    echo "Installing pip3..."
    if [[ "$OS" == "linux" ]]; then
        sudo apt install python3-pip || sudo dnf install python3-pip || sudo pacman -S python-pip
    fi
fi

# Check Node.js
if command -v node &> /dev/null; then
    NODE_VERSION=$(node --version)
    print_success "Node.js found: $NODE_VERSION"
else
    print_error "Node.js is not installed!"
    echo ""
    echo "Please install Node.js 18 or higher:"
    if [[ "$OS" == "linux" ]]; then
        echo "  curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -"
        echo "  sudo apt-get install -y nodejs"
    elif [[ "$OS" == "macos" ]]; then
        echo "  brew install node"
    fi
    echo "  Or download from: https://nodejs.org/"
    exit 1
fi

# Check npm
if command -v npm &> /dev/null; then
    NPM_VERSION=$(npm --version)
    print_success "npm found: $NPM_VERSION"
else
    print_error "npm is not installed!"
    exit 1
fi

echo ""
print_status "Installing Python dependencies..."
if pip3 install --break-system-packages -r requirements.txt; then
    print_success "Python dependencies installed"
else
    print_error "Failed to install Python dependencies"
    exit 1
fi

echo ""
print_status "Installing frontend dependencies..."
cd frontend
if npm install; then
    print_success "Frontend dependencies installed"
else
    print_error "Failed to install frontend dependencies"
    exit 1
fi
cd ..

echo ""
print_status "Installing root dependencies (concurrently for dev mode)..."
if npm install; then
    print_success "Root dependencies installed"
else
    print_error "Failed to install root dependencies"
    exit 1
fi

echo ""
echo -e "${YELLOW}=========================================="
echo "Build Mode"
echo "==========================================${NC}"
echo ""
echo "Choose how you want to run MeshNet:"
echo ""
echo "1) Development Mode (recommended for development)"
echo "   • Hot-reload frontend with 'npm run start'"
echo "   • Backend with 'python3 meshnet.py --dev'"
echo "   • Frontend runs on http://localhost:5173"
echo ""
echo "2) Production Mode (recommended for deployment)"
echo "   • Build frontend once"
echo "   • Single server on http://localhost:8080"
echo "   • Run with 'python3 meshnet.py'"
echo ""
read -p "Enter choice (1 for dev, 2 for production) [2]: " -n 1 -r
echo
BUILD_MODE=${REPLY:-2}

if [[ $BUILD_MODE == "2" ]]; then
    print_status "Building frontend for production..."
    if npm run build; then
        print_success "Frontend built successfully"
    else
        print_error "Failed to build frontend"
        exit 1
    fi
else
    print_success "Skipping production build (development mode)"
    print_warning "You'll need to run 'npm run start' in the frontend directory"
fi
cd ..

echo ""
print_status "Creating configuration..."
if [ ! -f config.json ]; then
    cp config.example.json config.json
    print_success "Created config.json from example"
    print_warning "Remember to edit config.json with your settings!"
else
    print_warning "config.json already exists, skipping..."
fi

echo ""
print_status "Creating necessary directories..."
mkdir -p data logs
print_success "Directories created"

echo ""
print_status "Setting permissions..."
chmod +x meshnet.py
chmod +x install_service.py
print_success "Permissions set"

# Offer to install Ollama
echo ""
echo -e "${YELLOW}=========================================="
echo "Optional: AI Assistant (Ollama)"
echo "==========================================${NC}"
echo ""
echo "MeshNet can use Ollama for local AI queries over the mesh."
echo "This is completely optional and privacy-focused (runs locally)."
echo ""
read -p "Would you like to install Ollama? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    print_status "Installing Ollama..."

    if [[ "$OS" == "linux" ]]; then
        if curl -fsSL https://ollama.com/install.sh | sh; then
            print_success "Ollama installed"

            # Start Ollama service
            if command -v systemctl &> /dev/null; then
                sudo systemctl start ollama || true
                sudo systemctl enable ollama || true
            fi

            echo ""
            print_status "Pulling a lightweight model (llama3.2:1b - ~1.3GB)..."
            echo "This may take a few minutes..."

            # Wait for Ollama to start
            sleep 3

            if ollama pull llama3.2:1b; then
                print_success "AI model downloaded"
            else
                print_warning "Failed to download model. You can do this later with: ollama pull llama3.2:1b"
            fi
        else
            print_warning "Failed to install Ollama. You can install it manually later."
        fi
    elif [[ "$OS" == "macos" ]]; then
        print_warning "Please install Ollama manually from: https://ollama.com/download"
        echo "After installing, run: ollama pull llama3.2:1b"
    fi
else
    print_status "Skipping Ollama installation"
    echo "You can install it later from: https://ollama.com"
fi

# Offer to install as system service (Linux only)
if [[ "$OS" == "linux" ]] && command -v systemctl &> /dev/null; then
    echo ""
    echo -e "${YELLOW}=========================================="
    echo "Optional: System Service Installation"
    echo "==========================================${NC}"
    echo ""
    echo "Install MeshNet as a system service to:"
    echo "  • Start automatically on boot"
    echo "  • Run in the background"
    echo "  • Manage with systemctl commands"
    echo ""
    read -p "Would you like to install as a system service? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        print_status "Installing system service..."
        if sudo python3 install_service.py; then
            print_success "Service installed"
            echo ""
            echo "Service commands:"
            echo "  Start:   sudo systemctl start meshnet"
            echo "  Stop:    sudo systemctl stop meshnet"
            echo "  Status:  sudo systemctl status meshnet"
            echo "  Logs:    sudo journalctl -u meshnet -f"
            echo ""
            read -p "Would you like to start the service now? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                sudo systemctl start meshnet
                print_success "Service started"
                echo ""
                echo -e "${GREEN}✓ MeshNet is now running!${NC}"
                echo "  Web UI: http://localhost:8080"
                echo "  Status: sudo systemctl status meshnet"
            fi
        else
            print_error "Failed to install service"
        fi
    else
        print_status "Skipping service installation"
    fi
fi

# Configure Reticulum
echo ""
echo -e "${YELLOW}=========================================="
echo "Reticulum Configuration"
echo "==========================================${NC}"
echo ""

# Ensure directory exists
mkdir -p ~/.reticulum

if [ ! -f ~/.reticulum/config ]; then
    print_status "Creating default Reticulum config..."
    cat > ~/.reticulum/config << 'EOF'
[reticulum]
  enable_transport = no
  share_instance = yes
  shared_instance_port = 37428
  instance_control_port = 37429

# No interfaces enabled by default for faster startup
# Uncomment and configure interfaces as needed:

# [[UDP Interface]]
#   type = UDPInterface
#   enabled = yes
#   listen_ip = 0.0.0.0
#   listen_port = 4242
#   forward_ip = 255.255.255.255
#   forward_port = 4242

# [[TCP Client]]
#   type = TCPClientInterface
#   enabled = no
#   target_host = hub.reticulum.network
#   target_port = 4965

# [[RNode]]
#   type = RNodeInterface
#   enabled = no
#   port = /dev/ttyUSB0
#   frequency = 867200000
#   bandwidth = 125000
#   txpower = 7
#   spreadingfactor = 8
#   codingrate = 5

# [[AutoInterface]]
#   type = AutoInterface
#   enabled = no
EOF
    print_success "Created default Reticulum config at ~/.reticulum/config"
    print_warning "Edit ~/.reticulum/config to add your specific interfaces"
else
    print_success "Reticulum config already exists at ~/.reticulum/config"
fi

# Final summary
echo ""
echo -e "${GREEN}=========================================="
echo "✓ Installation Complete!"
echo "==========================================${NC}"
echo ""
echo -e "${BLUE}Next Steps:${NC}"
echo ""
echo "1. Edit your configuration:"
echo "   nano config.json"
echo "   • Set your 'display_name' under lxmf"
echo "   • Configure AI, email, or Discord if desired"
echo ""
echo "2. Configure Reticulum interfaces (if needed):"
echo "   nano ~/.reticulum/config"
echo "   • Add RNode, TCP, or other interfaces"
echo ""
echo "3. Start MeshNet:"
if [[ "$OS" == "linux" ]] && systemctl is-active --quiet meshnet 2>/dev/null; then
    echo "   Already running as service!"
    echo "   • Web UI: http://localhost:8080"
    echo "   • Logs: sudo journalctl -u meshnet -f"
elif [[ $BUILD_MODE == "1" ]]; then
    echo "   Development mode:"
    echo "   npm start"
    echo "   • Starts both backend and frontend"
    echo "   • Web UI: http://localhost:5173"
else
    echo "   Production mode:"
    echo "   python3 meshnet.py"
    echo "   • Web UI: http://localhost:8080"
fi
echo ""
echo "4. Access the Web UI:"
if [[ $BUILD_MODE == "1" ]]; then
    echo "   Open your browser to: http://localhost:5173"
else
    echo "   Open your browser to: http://localhost:8080"
fi
echo ""

if command -v ollama &> /dev/null; then
    echo -e "${BLUE}AI Commands (if enabled in config):${NC}"
    echo "  #ai <question>    - Ask the AI assistant"
    echo "  #ask <question>   - Alternative AI command"
    echo ""
fi

echo -e "${BLUE}Other Commands:${NC}"
echo "  #help      - Show available commands"
echo "  #status    - System status"
echo "  #nodes     - Show known nodes"
echo "  #paths     - Show routing paths"
echo "  #interfaces - Show RNS interfaces"
echo ""

echo -e "${BLUE}Documentation:${NC}"
echo "  README.md  - Full documentation"
echo "  config.example.json - Configuration reference"
echo ""

print_success "Happy meshing! 🌐"
echo ""
