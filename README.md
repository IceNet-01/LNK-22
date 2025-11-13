# MeshNet - Reticulum Network System

A fully-featured web-based Reticulum (RNS) communication system with LXMF messaging, AI assistance, and comprehensive network management.

## 🚀 Quick Start

**One-command installation:**

```bash
git clone <your-repo-url> MeshNet
cd MeshNet
./install.sh
```

That's it! The installer will:
- ✅ Check and guide you through installing prerequisites
- ✅ Install all Python and Node.js dependencies
- ✅ Build the web interface
- ✅ Create configuration files
- ✅ Optionally install Ollama (AI assistant)
- ✅ Optionally set up as a system service

Then simply run:
```bash
python3 meshnet.py
```

Access the web UI at: **http://localhost:8080**

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

### Starting MeshNet

```bash
# Standard mode
python3 meshnet.py

# Custom config file
python3 meshnet.py --config /path/to/config.json

# As a system service (if installed)
sudo systemctl start meshnet
sudo systemctl status meshnet
sudo journalctl -u meshnet -f  # View logs
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

1. Open web UI at http://localhost:8080
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

## 📊 System Service (Linux)

The installer can set up MeshNet as a system service:

```bash
# Start service
sudo systemctl start meshnet

# Stop service
sudo systemctl stop meshnet

# Restart service
sudo systemctl restart meshnet

# Enable auto-start on boot
sudo systemctl enable meshnet

# Disable auto-start
sudo systemctl disable meshnet

# View status
sudo systemctl status meshnet

# View logs
sudo journalctl -u meshnet -f
```

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
Make sure you ran the installer which builds the frontend:
```bash
cd frontend
npm install
npm run build
cd ..
```

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
