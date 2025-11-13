# MeshNet - Reticulum Network System

A fully-featured web-based Reticulum (RNS) communication system with LXMF messaging, AI assistance, and comprehensive network management.

## 🚀 Quick Start

### Option 1: Quick Install (Prerequisites Already Installed)

If you already have Python 3 and Node.js installed:

```bash
git clone https://github.com/IceNet-01/MeshNet.git
cd MeshNet
./quick-install.sh
npm start
```

Access at: **http://localhost:5173**

### Option 2: Full Installation (Guided Setup)

**One-command installation:**

```bash
git clone https://github.com/IceNet-01/MeshNet.git && cd MeshNet && ./install.sh
```

The installer will:
- ✅ Check and guide you through installing prerequisites
- ✅ Install all Python and Node.js dependencies
- ✅ Ask if you want development or production mode
- ✅ Build the web interface (if production)
- ✅ Create configuration files
- ✅ Optionally install Ollama (AI assistant)
- ✅ Optionally set up as a system service

### Development Mode

Perfect for development with hot-reload:

```bash
# Single command - starts both backend and frontend!
npm start
```

Access at: **http://localhost:5173**

This runs both:
- Backend (Python) with `--dev` flag on port 8080
- Frontend (Vite) with hot-reload on port 5173

**Alternative - separate terminals:**
```bash
# Terminal 1: Backend only
npm run start:backend
# or: python3 meshnet.py --dev

# Terminal 2: Frontend only
npm run start:frontend
# or: cd frontend && npm run dev
```

### Production Mode

Single server for deployment:

```bash
# Build frontend once (if not done during install)
npm run production
# or: cd frontend && npm run build

# Run unified server
python3 meshnet.py
```

Access at: **http://localhost:8080**

## ✨ Features

### Core Reticulum Capabilities
- **Multi-Interface Support**: Manage multiple RNS interfaces (TCP, UDP, Serial, I2C, RNode, etc.)
- **LXMF Messaging**: Asynchronous message delivery with propagation support
- **Transport Node**: Act as a transport node for the Reticulum network
- **Propagation Node**: Optional LXMF propagation node functionality
- **Identity Management**: Create and manage RNS identities and destinations
- **Announce System**: Monitor and respond to RNS announces
- **Link Management**: Establish and manage RNS links
- **Path Discovery**: Real-time path table and routing information

### Interactive Command System
Process commands with configurable prefix (default: `#`):
- **Information**: `#ping`, `#help`, `#status`, `#time`, `#uptime`, `#version`
- **Network**: `#nodes`, `#paths`, `#interfaces`, `#announces`
- **AI Queries**: `#ai [question]` or `#ask [question]`
- **Notifications**: `#email [message]`, `#discord [message]`, `#notify [message]`
- **System**: `#stats`, `#identity`, `#destinations`

### AI Assistant Integration
- Local AI powered by Ollama (privacy-focused, no cloud)
- Configurable models (optimized for bandwidth-constrained networks)
- Automatic response formatting for RNS/LXMF constraints
- Model management through web UI

### External Notifications
- **Email**: SMTP integration for alerts and messages
- **Discord**: Webhook support for status updates
- Combined notification system

### Web-Based GUI
- **Dashboard**: Network overview, interface status, recent messages
- **Messages**: Real-time LXMF message feed with delivery tracking
- **Network**: Node discovery, path table, announce monitoring
- **Interfaces**: RNS interface management and configuration
- **Configuration**: System settings, identity management, AI setup
- **Logs**: System logging and debugging

## 📋 Requirements

- **Python 3.8+**
- **Node.js 18+**
- **Git**

The installer will check for these and guide you if anything is missing.

## 🔧 Configuration

### Basic Setup

After installation, edit `config.json`:

```bash
nano config.json
```

**Important settings to customize:**

1. **Your node name** (shown to other nodes):
```json
"lxmf": {
  "display_name": "Your Node Name Here"
}
```

2. **Enable AI assistant** (requires Ollama):
```json
"ai": {
  "enabled": true,
  "model": "llama3.2:1b"
}
```

3. **Email notifications** (optional):
```json
"notifications": {
  "email": {
    "enabled": true,
    "smtp_server": "smtp.gmail.com",
    "smtp_port": 587,
    "username": "your-email@gmail.com",
    "password": "your-app-password",
    "from_address": "your-email@gmail.com",
    "to_address": "recipient@example.com"
  }
}
```

4. **Discord notifications** (optional):
```json
"discord": {
  "enabled": true,
  "webhook_url": "https://discord.com/api/webhooks/..."
}
```

### Reticulum Configuration

Configure your Reticulum interfaces in `~/.reticulum/config`:

```bash
nano ~/.reticulum/config
```

**Example interfaces:**

**Local network (UDP broadcast):**
```ini
[[UDP Interface]]
  type = UDPInterface
  enabled = yes
  listen_ip = 0.0.0.0
  listen_port = 4242
  forward_ip = 255.255.255.255
  forward_port = 4242
```

**LoRa radio (RNode):**
```ini
[[RNode]]
  type = RNodeInterface
  enabled = yes
  port = /dev/ttyUSB0
  frequency = 867200000
  bandwidth = 125000
  txpower = 7
  spreadingfactor = 8
  codingrate = 5
```

**Connect to remote node (TCP):**
```ini
[[TCP Client]]
  type = TCPClientInterface
  enabled = yes
  target_host = remote.node.address
  target_port = 4242
```

**I2P network:**
```ini
[[I2P]]
  type = I2PInterface
  enabled = yes
```

## 🎮 Usage

### Development Workflow

```bash
# Single command - starts both backend and frontend!
npm start

# Access at http://localhost:5173
```

The frontend will automatically reload when you make changes to the code.

**What `npm start` does:**
- Starts Python backend in dev mode (port 8080)
- Starts Vite frontend dev server (port 5173)
- Shows output from both in a single terminal
- Press `Ctrl+C` to stop both

### Production Deployment

```bash
# One-time: Build frontend
npm run production
# or: cd frontend && npm run build

# Run unified server
python3 meshnet.py

# Access at http://localhost:8080
```

### System Service (Linux)

```bash
# Start service
sudo systemctl start meshnet

# Stop service
sudo systemctl stop meshnet

# Restart service
sudo systemctl restart meshnet

# Enable auto-start on boot
sudo systemctl enable meshnet

# View status
sudo systemctl status meshnet

# View logs
sudo journalctl -u meshnet -f
```

### Using Commands

Send commands via LXMF messages using the `#` prefix:

```
#help              - Show all available commands
#status            - Show system status
#nodes             - List known nodes
#paths             - Show routing table
#interfaces        - List RNS interfaces
#ai What is mesh networking?  - Query AI
#email System online!         - Send email notification
#discord Node started         - Send Discord notification
```

### Sending Messages

1. Open web UI (http://localhost:8080 or :5173 in dev mode)
2. Go to **Messages** tab
3. Enter destination hash and message
4. Click **Send Message**

Or use the API:
```bash
curl -X POST http://localhost:8080/api/messages/send \
  -H "Content-Type: application/json" \
  -d '{
    "destination": "1234567890abcdef...",
    "content": "Hello from MeshNet!",
    "title": "Test Message"
  }'
```

## 🤖 AI Assistant Setup

If you want AI capabilities:

1. **Install Ollama** (done automatically if you chose it during installation):
```bash
curl -fsSL https://ollama.com/install.sh | sh
```

2. **Pull a model**:
```bash
# Recommended lightweight models for mesh networks
ollama pull llama3.2:1b      # 1.3 GB, fast
ollama pull phi3:mini        # 2.2 GB, quality
ollama pull tinyllama:latest # 637 MB, very fast
```

3. **Enable in config.json**:
```json
"ai": {
  "enabled": true,
  "ollama_host": "http://localhost:11434",
  "model": "llama3.2:1b"
}
```

4. **Use it**:
```
#ai What is the weather?
#ask How do I configure a new interface?
```

## 🔒 Security

- **End-to-end encryption**: All LXMF messages are encrypted
- **RNS cryptographic identities**: Secure identity management
- **Local AI processing**: No data sent to cloud services
- **Rate limiting**: Prevents command spam and abuse

## 🛠️ Troubleshooting

### Port already in use
Change the port in `config.json`:
```json
"server": {
  "port": 8081
}
```

### Can't connect to Reticulum
1. Check `~/.reticulum/config` has at least one enabled interface
2. Verify Reticulum is working: `python3 -c "import RNS; print(RNS.Reticulum())"`
3. Check logs for errors when starting MeshNet

### Frontend not loading
Make sure you built the frontend:
```bash
npm run production
# or: cd frontend && npm run build
```

### Dev mode CORS issues
In dev mode, the frontend (port 5173) proxies API requests to the backend (port 8080). Check `frontend/vite.config.js` if you have issues.

### AI not responding
1. Check Ollama is running: `curl http://localhost:11434/api/tags`
2. Verify model is installed: `ollama list`
3. Enable AI in `config.json`

### Permission errors
```bash
# Fix Reticulum directory permissions
sudo chown -R $USER:$USER ~/.reticulum

# Fix MeshNet directory permissions
sudo chown -R $USER:$USER .
```

## 📁 Project Structure

```
MeshNet/
├── backend/                  # Python backend
│   ├── core/                # Core RNS/LXMF modules
│   ├── services/            # AI and notification services
│   ├── server/              # WebSocket and REST API
│   └── utils/               # Configuration and utilities
├── frontend/                # React web interface
│   ├── src/                # Source code
│   └── dist/               # Built files (after npm run build)
├── data/                   # Runtime data
├── logs/                   # Log files
├── meshnet.py             # Main application
├── package.json           # Root npm scripts
├── config.json            # Your configuration
├── install.sh             # All-in-one installer
└── README.md              # This file
```

## 🌐 Architecture

### Backend (Python)
- **RNS Integration**: Full Reticulum Network Stack support
- **LXMF Messaging**: Asynchronous message handling
- **WebSocket Server**: Real-time updates to web clients
- **REST API**: Configuration and control endpoints
- **Command Processing**: Rate-limited command system
- **AI Service**: Local Ollama integration
- **Notifications**: Email (SMTP) and Discord webhooks

### Frontend (React)
- **Real-time Dashboard**: System status and statistics
- **Message Interface**: Send and receive LXMF messages
- **Network Monitor**: Nodes, paths, and interfaces
- **Configuration UI**: Live settings management
- **Log Viewer**: System logs with filtering

## 📦 NPM Scripts

Root level (`npm run <script>`):
- `start` - Start frontend dev server (same as `dev`)
- `dev` - Start frontend dev server with hot-reload
- `build` - Build frontend for production
- `production` - Build frontend for production (alias)

Frontend level (`cd frontend && npm run <script>`):
- `start` - Start Vite dev server
- `dev` - Start Vite dev server (same as `start`)
- `build` - Build for production
- `production` - Build for production (alias)
- `preview` - Preview production build locally

## 🤝 Contributing

Contributions are welcome! This is a pure Reticulum implementation focused on RNS/LXMF capabilities.

## 📄 License

MIT License - see LICENSE file for details

## 🙏 Acknowledgments

Inspired by:
- [Reticulum Network Stack](https://reticulum.network/) by Mark Qvist
- [NomadNet](https://github.com/markqvist/NomadNet)
- [LXMF](https://github.com/markqvist/LXMF)
- [Mesh-Bridge-GUI](https://github.com/IceNet-01/Mesh-Bridge-GUI)

## 📞 Support

- Check the documentation in this README
- Review `config.example.json` for all configuration options
- Examine logs for error messages
- Ensure Reticulum is properly configured

---

**Built for the Reticulum mesh network. 100% dedicated to RNS/LXMF. No Meshtastic dependencies.**
