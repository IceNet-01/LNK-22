#!/usr/bin/env python3
"""
LNK-22 LAN Discovery Signaling Server

A simple WebSocket signaling server for WebRTC peer discovery on local networks.
Run this on any machine on your LAN, and web clients will auto-discover each other.

Usage:
    python3 signaling_server.py [port]

Default port is 8765.
"""

import asyncio
import json
import sys
from datetime import datetime

try:
    import websockets
except ImportError:
    print("Error: websockets library not found.")
    print("Install with: pip3 install websockets")
    sys.exit(1)

# Connected clients: { client_id: websocket }
clients = {}

# Client info: { client_id: { name, last_seen } }
client_info = {}


async def register(websocket, client_id, name=None):
    """Register a new client."""
    clients[client_id] = websocket
    client_info[client_id] = {
        'name': name or client_id,
        'last_seen': datetime.now().isoformat()
    }
    print(f"[+] Client registered: {client_id} ({name or 'unnamed'})")
    print(f"    Total clients: {len(clients)}")


async def unregister(client_id):
    """Unregister a client."""
    if client_id in clients:
        del clients[client_id]
    if client_id in client_info:
        del client_info[client_id]
    print(f"[-] Client unregistered: {client_id}")
    print(f"    Total clients: {len(clients)}")

    # Notify other clients
    leave_msg = json.dumps({
        'type': 'leave',
        'from': client_id
    })
    await broadcast(leave_msg, exclude=client_id)


async def broadcast(message, exclude=None):
    """Broadcast message to all clients except the excluded one."""
    for client_id, websocket in list(clients.items()):
        if client_id != exclude:
            try:
                await websocket.send(message)
            except websockets.exceptions.ConnectionClosed:
                await unregister(client_id)


async def send_to(client_id, message):
    """Send message to a specific client."""
    if client_id in clients:
        try:
            await clients[client_id].send(message)
            return True
        except websockets.exceptions.ConnectionClosed:
            await unregister(client_id)
    return False


async def handler(websocket, path):
    """Handle WebSocket connections."""
    client_id = None

    try:
        async for raw_message in websocket:
            try:
                message = json.loads(raw_message)
            except json.JSONDecodeError:
                print(f"[!] Invalid JSON received")
                continue

            msg_type = message.get('type')
            from_id = message.get('from')

            # First message should identify the client
            if client_id is None:
                if from_id:
                    client_id = from_id
                    await register(websocket, client_id, message.get('name'))
                else:
                    print("[!] Message without 'from' field, ignoring")
                    continue

            # Update last seen
            if client_id in client_info:
                client_info[client_id]['last_seen'] = datetime.now().isoformat()
                if message.get('name'):
                    client_info[client_id]['name'] = message.get('name')

            # Handle message types
            if msg_type == 'announce':
                # Broadcast announcement to all other clients
                print(f"[*] Announce from {client_id}: {message.get('name', 'unnamed')}")
                await broadcast(raw_message, exclude=client_id)

            elif msg_type in ['offer', 'answer', 'ice-candidate']:
                # Relay to specific peer
                to_id = message.get('to')
                if to_id:
                    success = await send_to(to_id, raw_message)
                    if not success:
                        print(f"[!] Failed to relay {msg_type} to {to_id}")
                else:
                    print(f"[!] {msg_type} message missing 'to' field")

            elif msg_type == 'broadcast':
                # Broadcast to all clients
                await broadcast(raw_message, exclude=client_id)

            else:
                # Unknown message type, broadcast it
                print(f"[?] Unknown message type: {msg_type}")
                await broadcast(raw_message, exclude=client_id)

    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        if client_id:
            await unregister(client_id)


async def main(port):
    """Start the signaling server."""
    print(f"""
========================================
  LNK-22 LAN Discovery Signaling Server
========================================

Listening on ws://0.0.0.0:{port}

Web clients should connect to:
  ws://<this-machine-ip>:{port}

Press Ctrl+C to stop.
----------------------------------------
""")

    async with websockets.serve(handler, "0.0.0.0", port):
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

    try:
        asyncio.run(main(port))
    except KeyboardInterrupt:
        print("\n\nServer stopped.")
