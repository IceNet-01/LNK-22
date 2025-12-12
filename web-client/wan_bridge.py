#!/usr/bin/env python3
"""
LNK-22 WAN Bridge Server

A persistent bridge server that connects LNK-22 mesh networks across the internet.
Supports encrypted tunnels, mesh routing, and multi-site connectivity.

Features:
- WebSocket-based secure connections
- Site-to-site mesh routing
- Encrypted message relay
- Auto-reconnection for clients
- Message deduplication
- Health monitoring

Usage:
    python3 wan_bridge.py [port] [--ssl-cert cert.pem] [--ssl-key key.pem]

Default port is 9000.
For production, use SSL certificates for encryption.
"""

import asyncio
import json
import sys
import hashlib
import time
import argparse
import ssl
from datetime import datetime, timedelta
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Set, Optional

try:
    import websockets
except ImportError:
    print("Error: websockets library not found.")
    print("Install with: pip3 install websockets")
    sys.exit(1)


@dataclass
class Site:
    """Represents a connected mesh site/network."""
    site_id: str
    websocket: object
    name: str = ""
    nodes: Set[str] = field(default_factory=set)  # Node addresses at this site
    connected_at: datetime = field(default_factory=datetime.now)
    last_heartbeat: datetime = field(default_factory=datetime.now)
    messages_relayed: int = 0
    bytes_relayed: int = 0


class WANBridge:
    """WAN Bridge Server for cross-site mesh connectivity."""

    def __init__(self):
        # Connected sites: { site_id: Site }
        self.sites: Dict[str, Site] = {}

        # Message deduplication: { message_hash: timestamp }
        self.seen_messages: Dict[str, float] = {}
        self.message_ttl = 60  # seconds

        # Routing table: { node_address: site_id }
        self.routing_table: Dict[str, str] = {}

        # Statistics
        self.total_messages = 0
        self.total_bytes = 0
        self.start_time = datetime.now()

    async def register_site(self, websocket, site_id: str, name: str = "") -> Site:
        """Register a new site connection."""
        site = Site(
            site_id=site_id,
            websocket=websocket,
            name=name or f"Site-{site_id[:8]}"
        )
        self.sites[site_id] = site
        print(f"[+] Site registered: {site.name} ({site_id[:16]}...)")
        print(f"    Total sites: {len(self.sites)}")

        # Notify other sites
        await self.broadcast_site_update()
        return site

    async def unregister_site(self, site_id: str):
        """Unregister a site."""
        if site_id in self.sites:
            site = self.sites[site_id]
            print(f"[-] Site disconnected: {site.name}")

            # Remove node routes for this site
            self.routing_table = {
                node: sid for node, sid in self.routing_table.items()
                if sid != site_id
            }

            del self.sites[site_id]
            print(f"    Total sites: {len(self.sites)}")

            # Notify other sites
            await self.broadcast({
                'type': 'site_left',
                'site_id': site_id,
                'name': site.name
            }, exclude=site_id)

    async def broadcast_site_update(self):
        """Broadcast current site list to all connected sites."""
        sites_info = []
        for sid, site in self.sites.items():
            sites_info.append({
                'site_id': sid[:16],  # Truncate for privacy
                'name': site.name,
                'nodes': len(site.nodes),
                'connected_since': site.connected_at.isoformat()
            })

        await self.broadcast({
            'type': 'sites_update',
            'sites': sites_info,
            'total_sites': len(self.sites)
        })

    async def broadcast(self, message: dict, exclude: str = None):
        """Broadcast message to all sites except excluded one."""
        msg_str = json.dumps(message)
        for site_id, site in list(self.sites.items()):
            if site_id != exclude:
                try:
                    await site.websocket.send(msg_str)
                except websockets.exceptions.ConnectionClosed:
                    await self.unregister_site(site_id)

    async def relay_to_site(self, target_site_id: str, message: dict) -> bool:
        """Relay message to a specific site."""
        if target_site_id in self.sites:
            try:
                site = self.sites[target_site_id]
                msg_str = json.dumps(message)
                await site.websocket.send(msg_str)
                site.messages_relayed += 1
                site.bytes_relayed += len(msg_str)
                return True
            except websockets.exceptions.ConnectionClosed:
                await self.unregister_site(target_site_id)
        return False

    def deduplicate_message(self, message: dict) -> bool:
        """Check if message is duplicate. Returns True if new, False if duplicate."""
        # Create hash of message content
        content = json.dumps(message, sort_keys=True)
        msg_hash = hashlib.sha256(content.encode()).hexdigest()[:16]

        # Clean old messages
        now = time.time()
        self.seen_messages = {
            h: t for h, t in self.seen_messages.items()
            if now - t < self.message_ttl
        }

        # Check if seen
        if msg_hash in self.seen_messages:
            return False  # Duplicate

        self.seen_messages[msg_hash] = now
        return True  # New message

    def update_routing(self, site_id: str, nodes: list):
        """Update routing table with nodes from a site."""
        for node in nodes:
            self.routing_table[node] = site_id
        if site_id in self.sites:
            self.sites[site_id].nodes = set(nodes)

    def find_route(self, dest_node: str) -> Optional[str]:
        """Find which site a node belongs to."""
        return self.routing_table.get(dest_node)

    async def handle_message(self, site_id: str, message: dict):
        """Handle incoming message from a site."""
        msg_type = message.get('type', '')

        if msg_type == 'heartbeat':
            # Update heartbeat
            if site_id in self.sites:
                self.sites[site_id].last_heartbeat = datetime.now()
            return

        elif msg_type == 'nodes_update':
            # Site reporting its nodes
            nodes = message.get('nodes', [])
            self.update_routing(site_id, nodes)
            print(f"[*] {self.sites[site_id].name}: {len(nodes)} nodes")
            return

        elif msg_type == 'mesh_message':
            # Message to relay across WAN
            if not self.deduplicate_message(message):
                return  # Duplicate

            self.total_messages += 1

            dest = message.get('dest')
            if dest:
                # Unicast - find route
                target_site = self.find_route(dest)
                if target_site and target_site != site_id:
                    await self.relay_to_site(target_site, message)
                else:
                    # Broadcast to all other sites
                    await self.broadcast(message, exclude=site_id)
            else:
                # Broadcast to all sites
                await self.broadcast(message, exclude=site_id)

        elif msg_type == 'query_sites':
            # Return list of connected sites
            await self.relay_to_site(site_id, {
                'type': 'sites_list',
                'sites': [
                    {'site_id': sid[:16], 'name': s.name, 'nodes': len(s.nodes)}
                    for sid, s in self.sites.items()
                ]
            })

        else:
            # Unknown message type - relay as-is
            if self.deduplicate_message(message):
                await self.broadcast(message, exclude=site_id)

    def get_stats(self) -> dict:
        """Get bridge statistics."""
        uptime = datetime.now() - self.start_time
        return {
            'uptime_seconds': int(uptime.total_seconds()),
            'uptime_str': str(uptime).split('.')[0],
            'total_sites': len(self.sites),
            'total_nodes': len(self.routing_table),
            'total_messages': self.total_messages,
            'total_bytes': self.total_bytes,
            'sites': [
                {
                    'name': s.name,
                    'nodes': len(s.nodes),
                    'messages': s.messages_relayed,
                    'connected': s.connected_at.isoformat()
                }
                for s in self.sites.values()
            ]
        }


# Global bridge instance
bridge = WANBridge()


async def handler(websocket, path):
    """Handle WebSocket connections from sites."""
    site_id = None

    try:
        # Wait for registration message
        async for raw_message in websocket:
            try:
                message = json.loads(raw_message)
            except json.JSONDecodeError:
                print(f"[!] Invalid JSON received")
                continue

            # First message must register the site
            if site_id is None:
                if message.get('type') == 'register':
                    site_id = message.get('site_id')
                    name = message.get('name', '')
                    if not site_id:
                        await websocket.send(json.dumps({
                            'type': 'error',
                            'message': 'site_id required'
                        }))
                        return

                    await bridge.register_site(websocket, site_id, name)

                    # Send welcome message
                    await websocket.send(json.dumps({
                        'type': 'registered',
                        'site_id': site_id,
                        'message': f'Welcome to LNK-22 WAN Bridge'
                    }))

                    # Send current sites
                    await websocket.send(json.dumps({
                        'type': 'sites_list',
                        'sites': [
                            {'site_id': sid[:16], 'name': s.name, 'nodes': len(s.nodes)}
                            for sid, s in bridge.sites.items()
                            if sid != site_id
                        ]
                    }))
                else:
                    await websocket.send(json.dumps({
                        'type': 'error',
                        'message': 'Must register first'
                    }))
                continue

            # Handle subsequent messages
            await bridge.handle_message(site_id, message)
            bridge.total_bytes += len(raw_message)

    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        if site_id:
            await bridge.unregister_site(site_id)


async def heartbeat_checker():
    """Periodically check for dead connections."""
    while True:
        await asyncio.sleep(30)
        now = datetime.now()
        for site_id, site in list(bridge.sites.items()):
            if now - site.last_heartbeat > timedelta(seconds=90):
                print(f"[!] Site {site.name} timed out")
                await bridge.unregister_site(site_id)


async def stats_reporter():
    """Periodically report stats."""
    while True:
        await asyncio.sleep(60)
        stats = bridge.get_stats()
        print(f"[STATS] Sites: {stats['total_sites']}, "
              f"Nodes: {stats['total_nodes']}, "
              f"Messages: {stats['total_messages']}, "
              f"Uptime: {stats['uptime_str']}")


async def main(port: int, ssl_context=None):
    """Start the WAN bridge server."""
    protocol = "wss" if ssl_context else "ws"

    print(f"""
========================================
    LNK-22 WAN Bridge Server
========================================

Listening on {protocol}://0.0.0.0:{port}

Web clients should connect with:
  Site ID: Unique identifier for your site
  Name: Human-readable site name

Example client connection:
  {{
    "type": "register",
    "site_id": "site-abc123...",
    "name": "Home Base"
  }}

Press Ctrl+C to stop.
----------------------------------------
""")

    # Start background tasks
    asyncio.create_task(heartbeat_checker())
    asyncio.create_task(stats_reporter())

    # Start WebSocket server
    async with websockets.serve(handler, "0.0.0.0", port, ssl=ssl_context):
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='LNK-22 WAN Bridge Server')
    parser.add_argument('port', nargs='?', type=int, default=9000,
                        help='Port to listen on (default: 9000)')
    parser.add_argument('--ssl-cert', help='SSL certificate file')
    parser.add_argument('--ssl-key', help='SSL key file')

    args = parser.parse_args()

    ssl_context = None
    if args.ssl_cert and args.ssl_key:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain(args.ssl_cert, args.ssl_key)
        print("[*] SSL enabled")

    try:
        asyncio.run(main(args.port, ssl_context))
    except KeyboardInterrupt:
        print("\n\nServer stopped.")
