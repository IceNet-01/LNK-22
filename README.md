# MeshNet - Reticulum Network System

A fully-featured web-based Reticulum (RNS) communication system with LXMF messaging, AI assistance, and comprehensive network management.

## Features

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

## Architecture

### Backend (Python)
- RNS/Reticulum core integration
- LXMF message handling and propagation
- WebSocket server for real-time updates
- REST API for configuration and control
- Command processing system
- AI and notification services

### Frontend (React + Vite)
- Modern, responsive web interface
- Real-time updates via WebSocket
- Multi-tab dashboard
- Configuration management UI

## Installation

### Requirements
- Python 3.8+
- Node.js 18+
- RNS (Reticulum Network Stack)
- LXMF (Lightweight Extensible Message Format)

### Setup

```bash
# Clone the repository
git clone <repository-url>
cd MeshNet

# Install Python dependencies
pip install -r requirements.txt

# Install frontend dependencies
cd frontend
npm install
cd ..

# Configure the system
cp config.example.json config.json
# Edit config.json with your settings

# Run in development mode
python meshnet.py --dev

# Or build for production
cd frontend
npm run build
cd ..
python meshnet.py
```

### Systemd Service (Linux)

```bash
# Install as system service
sudo python install_service.py

# Manage service
sudo systemctl start meshnet
sudo systemctl enable meshnet
sudo systemctl status meshnet
```

## Configuration

The system uses `config.json` for configuration:

```json
{
  "reticulum": {
    "config_path": "~/.reticulum",
    "enable_transport": true,
    "enable_propagation": false
  },
  "lxmf": {
    "display_name": "MeshNet Node",
    "announce_interval": 900,
    "propagation_node": false
  },
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "dev_mode": false
  },
  "commands": {
    "prefix": "#",
    "rate_limit": {
      "commands_per_minute": 10,
      "ai_queries_per_minute": 3
    }
  },
  "ai": {
    "enabled": true,
    "ollama_host": "http://localhost:11434",
    "model": "llama3.2:1b",
    "max_response_length": 500
  },
  "notifications": {
    "email": {
      "enabled": false,
      "smtp_server": "",
      "smtp_port": 587,
      "username": "",
      "password": "",
      "from_address": "",
      "to_address": ""
    },
    "discord": {
      "enabled": false,
      "webhook_url": ""
    }
  }
}
```

## Usage

### Single Node Mode
Even with one RNS interface, you can:
- Send and receive LXMF messages
- Query AI assistant over the mesh
- Send notifications via mesh commands
- Monitor network announces and paths
- Access full mesh networking capabilities

### Multi-Interface Mode
- Bridge between different RNS interfaces
- Act as transport node for the network
- Provide propagation services for LXMF
- Create redundant network paths

### Command Examples

```
# Get node information
#status

# Query AI assistant
#ai What is the weather like?

# Send email notification
#email System is online and functioning

# Send Discord notification
#discord MeshNet node started successfully

# View network nodes
#nodes

# Check routing paths
#paths

# List interfaces
#interfaces
```

## Technical Details

### RNS Integration
- Full Reticulum Network Stack support
- Configurable interface types
- Transport node capabilities
- Announce handling and response
- Link establishment and management

### LXMF Messaging
- Asynchronous message delivery
- Message persistence
- Delivery confirmations
- Propagation node support
- Message routing and forwarding

### Security
- RNS cryptographic identity system
- End-to-end encrypted messaging via LXMF
- Secure link establishment
- Optional propagation encryption

## Development

### Development Mode
```bash
# Terminal 1: Run backend with auto-reload
python meshnet.py --dev

# Terminal 2: Run frontend dev server
cd frontend
npm run dev
```

Frontend dev server: http://localhost:5173
Backend API/WebSocket: http://localhost:8080

### Building for Production
```bash
cd frontend
npm run build
cd ..
# Frontend built to frontend/dist, served by backend
```

## License

MIT License - see LICENSE file for details

## Acknowledgments

Inspired by:
- [Reticulum Network Stack](https://reticulum.network/)
- [NomadNet](https://github.com/markqvist/NomadNet)
- [meshchat](https://github.com/liamcottle/meshchat)
