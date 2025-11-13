"""LXMF (Lightweight Extensible Message Format) integration"""

import RNS
import LXMF
import time
from typing import Optional, Callable, List, Dict
import logging

logger = logging.getLogger(__name__)


class LXMFCore:
    """LXMF messaging system integration"""

    def __init__(self, reticulum_core, display_name: str = "MeshNet Node",
                 announce_interval: int = 900, enable_propagation: bool = False):
        self.reticulum_core = reticulum_core
        self.display_name = display_name
        self.announce_interval = announce_interval
        self.enable_propagation = enable_propagation

        self.router: Optional[LXMF.LXMRouter] = None
        self.destination: Optional[RNS.Destination] = None
        self.started = False

        # Message tracking
        self.messages_received = 0
        self.messages_sent = 0
        self.message_history: List[Dict] = []

        # Callbacks
        self.on_message_callbacks: List[Callable] = []

        # Propagation node (if enabled)
        self.propagation_node: Optional[LXMF.LXMPeer] = None

    def start(self):
        """Initialize and start LXMF"""
        try:
            logger.info("Starting LXMF messaging system...")

            if not self.reticulum_core.identity:
                raise RuntimeError("Reticulum identity not initialized")

            # Create LXMF router
            self.router = LXMF.LXMRouter(
                identity=self.reticulum_core.identity,
                storagepath=self.reticulum_core.config_path
            )

            # Register delivery callback for incoming messages
            self.router.register_delivery_callback(self._on_message_received)

            # Register delivery identity and get destination
            self.destination = self.router.register_delivery_identity(
                self.reticulum_core.identity
            )

            # Start propagation node if enabled
            if self.enable_propagation:
                self._start_propagation_node()

            self.started = True
            logger.info(f"LXMF started - Destination: {RNS.prettyhexrep(self.destination.hash)}")

            # Announce ourselves
            self.announce()

        except Exception as e:
            logger.error(f"Error starting LXMF: {e}")
            raise

    def stop(self):
        """Stop LXMF"""
        try:
            logger.info("Stopping LXMF...")

            if self.propagation_node:
                try:
                    self.propagation_node.stop()
                except Exception as e:
                    logger.error(f"Error stopping propagation node: {e}")

            self.started = False
            logger.info("LXMF stopped")

        except Exception as e:
            logger.error(f"Error stopping LXMF: {e}")

    def announce(self, app_data: Optional[bytes] = None):
        """Announce our LXMF destination"""
        try:
            if not self.destination:
                return

            # Prepare announce data with display name
            announce_data = self.display_name.encode('utf-8')
            if app_data:
                announce_data = app_data

            self.destination.announce(announce_data)
            logger.info(f"Announced LXMF destination with name: {self.display_name}")

        except Exception as e:
            logger.error(f"Error announcing LXMF destination: {e}")

    def send_message(self, destination_hash: str, content: str,
                     title: str = "", fields: Optional[Dict] = None) -> bool:
        """Send an LXMF message"""
        try:
            if not self.router:
                logger.error("LXMF router not initialized")
                return False

            # Convert destination hash from hex string to bytes
            if isinstance(destination_hash, str):
                destination_hash = bytes.fromhex(destination_hash.replace(':', ''))

            # Create outbound message
            lxm = LXMF.LXMessage(
                destination=destination_hash,
                source=self.destination,
                content=content,
                title=title,
                fields=fields or {}
            )

            # Send via router
            self.router.handle_outbound(lxm)
            self.messages_sent += 1

            # Store in history
            self._store_message({
                "type": "sent",
                "destination": RNS.prettyhexrep(destination_hash),
                "content": content,
                "title": title,
                "timestamp": time.time(),
                "state": "pending"
            })

            logger.info(f"Sent message to {RNS.prettyhexrep(destination_hash)}")
            return True

        except Exception as e:
            logger.error(f"Error sending message: {e}")
            return False

    def _on_message_received(self, lxm):
        """Handle received LXMF message"""
        try:
            self.messages_received += 1

            # Extract message data
            message_data = {
                "type": "received",
                "source": RNS.prettyhexrep(lxm.source_hash),
                "destination": RNS.prettyhexrep(lxm.destination_hash),
                "content": lxm.content.decode('utf-8') if isinstance(lxm.content, bytes) else lxm.content,
                "title": lxm.title.decode('utf-8') if isinstance(lxm.title, bytes) else lxm.title,
                "timestamp": lxm.timestamp if hasattr(lxm, 'timestamp') else time.time(),
                "rssi": lxm.rssi if hasattr(lxm, 'rssi') else None,
                "snr": lxm.snr if hasattr(lxm, 'snr') else None
            }

            # Store in history
            self._store_message(message_data)

            # Call registered callbacks
            for callback in self.on_message_callbacks:
                try:
                    callback(message_data)
                except Exception as e:
                    logger.error(f"Error in message callback: {e}")

            logger.info(f"Received message from {message_data['source']}: {message_data['content'][:50]}...")

        except Exception as e:
            logger.error(f"Error handling received message: {e}")

    def _delivery_callback(self, lxm):
        """Handle delivery confirmation"""
        try:
            destination = RNS.prettyhexrep(lxm.destination_hash)
            logger.info(f"Message delivered to {destination}")

            # Update message history
            for msg in self.message_history:
                if msg.get('destination') == destination and msg.get('state') == 'pending':
                    msg['state'] = 'delivered'
                    msg['delivered_at'] = time.time()
                    break

        except Exception as e:
            logger.error(f"Error in delivery callback: {e}")

    def _announce_handler(self, destination_hash, announced_identity, app_data):
        """Handle announces from other LXMF nodes"""
        try:
            display_name = app_data.decode('utf-8') if app_data else "Unknown"
            logger.info(f"Received announce from {RNS.prettyhexrep(destination_hash)}: {display_name}")

            # Could store known nodes here for future use

        except Exception as e:
            logger.error(f"Error handling announce: {e}")

    def _start_propagation_node(self):
        """Start LXMF propagation node"""
        try:
            logger.info("Starting LXMF propagation node...")
            self.propagation_node = LXMF.LXMPeer(
                identity=self.reticulum_core.identity,
                router=self.router
            )
            logger.info("Propagation node started")
        except Exception as e:
            logger.error(f"Error starting propagation node: {e}")

    def _store_message(self, message_data: Dict):
        """Store message in history"""
        self.message_history.append(message_data)

        # Limit history size
        max_messages = 1000
        if len(self.message_history) > max_messages:
            self.message_history = self.message_history[-max_messages:]

    def register_message_callback(self, callback: Callable):
        """Register callback for incoming messages"""
        self.on_message_callbacks.append(callback)

    def get_messages(self, limit: Optional[int] = None) -> List[Dict]:
        """Get message history"""
        if limit:
            return self.message_history[-limit:]
        return self.message_history

    def get_destination_hash(self) -> str:
        """Get our LXMF destination hash"""
        if self.destination:
            return RNS.prettyhexrep(self.destination.hash)
        return ""

    def get_stats(self) -> Dict:
        """Get LXMF statistics"""
        return {
            "started": self.started,
            "destination": self.get_destination_hash(),
            "display_name": self.display_name,
            "messages_received": self.messages_received,
            "messages_sent": self.messages_sent,
            "message_history_count": len(self.message_history),
            "propagation_enabled": self.enable_propagation,
            "announce_interval": self.announce_interval
        }
