"""Reticulum Network Stack integration"""

import RNS
import time
import os
from typing import Dict, List, Optional, Callable
import logging

logger = logging.getLogger(__name__)


class ReticulumCore:
    """Core Reticulum integration and management"""

    def __init__(self, config_path: Optional[str] = None, enable_transport: bool = True):
        self.config_path = config_path or os.path.expanduser("~/.reticulum")
        self.enable_transport = enable_transport
        self.reticulum: Optional[RNS.Reticulum] = None
        self.identity: Optional[RNS.Identity] = None
        self.started = False
        self.start_time = time.time()

        # Callbacks
        self.on_announce_callbacks: List[Callable] = []
        self.on_link_callbacks: List[Callable] = []

        # Tracking
        self.known_destinations: Dict[bytes, Dict] = {}
        self.active_links: Dict[str, RNS.Link] = {}

    def start(self):
        """Initialize and start Reticulum"""
        try:
            logger.info("Starting Reticulum Network Stack...")
            logger.info(f"Config directory: {self.config_path}")

            # Expand user path
            config_dir = os.path.expanduser(self.config_path)

            # Check if config directory exists
            if not os.path.exists(config_dir):
                logger.warning(f"Reticulum config directory doesn't exist: {config_dir}")
                logger.info("Creating config directory...")
                os.makedirs(config_dir, exist_ok=True)

            # Check if config file exists
            config_file = os.path.join(config_dir, "config")
            if not os.path.exists(config_file):
                logger.error(f"Reticulum config file not found: {config_file}")
                logger.error("Please run the installer or manually create ~/.reticulum/config")
                raise RuntimeError(f"Reticulum config file not found: {config_file}")

            logger.info("Initializing Reticulum instance...")

            # Initialize Reticulum with timeout handling
            try:
                self.reticulum = RNS.Reticulum(
                    configdir=config_dir,
                    loglevel=RNS.LOG_INFO
                )
                logger.info("Reticulum instance created successfully")
            except Exception as rns_error:
                logger.error(f"Failed to initialize RNS.Reticulum: {rns_error}")
                logger.error("This usually means there's an issue with your Reticulum config")
                raise

            # Enable transport if configured
            if self.enable_transport:
                self.reticulum.transport_enabled()
                logger.info("Transport mode enabled")

            # Load or create identity
            logger.info("Loading or creating identity...")
            identity_path = os.path.join(config_dir, "identities", "meshnet")
            if os.path.exists(identity_path):
                self.identity = RNS.Identity.from_file(identity_path)
                logger.info("Loaded existing identity")
            else:
                logger.info("Creating new identity...")
                self.identity = RNS.Identity()
                os.makedirs(os.path.dirname(identity_path), exist_ok=True)
                self.identity.to_file(identity_path)
                logger.info("Created new identity")

            self.started = True
            self.start_time = time.time()
            logger.info(f"Reticulum started successfully - Hash: {RNS.prettyhexrep(self.identity.hash)}")

        except Exception as e:
            logger.error(f"Error starting Reticulum: {e}")
            logger.error("Traceback:", exc_info=True)
            raise

    def stop(self):
        """Stop Reticulum"""
        try:
            logger.info("Stopping Reticulum...")

            # Close all active links
            for link_id, link in list(self.active_links.items()):
                try:
                    link.teardown()
                except Exception as e:
                    logger.error(f"Error closing link {link_id}: {e}")

            self.active_links.clear()
            self.started = False

            logger.info("Reticulum stopped")

        except Exception as e:
            logger.error(f"Error stopping Reticulum: {e}")

    def get_identity_info(self) -> Dict:
        """Get identity information"""
        if not self.identity:
            return {}

        return {
            "hash": RNS.prettyhexrep(self.identity.hash),
            "hash_raw": self.identity.hash.hex(),
            "public_key": self.identity.get_public_key().hex(),
            "can_sign": self.identity.can_sign(),
            "can_encrypt": self.identity.can_encrypt()
        }

    def get_interface_stats(self) -> List[Dict]:
        """Get statistics for all interfaces"""
        if not self.reticulum:
            return []

        stats = []
        for interface in self.reticulum.interfaces:
            interface_stats = {
                "name": interface.name,
                "mode": interface.mode if hasattr(interface, 'mode') else "Unknown",
                "online": interface.online if hasattr(interface, 'online') else False,
                "rxb": interface.rxb if hasattr(interface, 'rxb') else 0,
                "txb": interface.txb if hasattr(interface, 'txb') else 0,
            }

            # Add bitrate if available
            if hasattr(interface, 'bitrate'):
                interface_stats["bitrate"] = interface.bitrate

            # Add status string
            if hasattr(interface, 'status_string'):
                interface_stats["status"] = interface.status_string()

            stats.append(interface_stats)

        return stats

    def get_path_table(self) -> List[Dict]:
        """Get the current path table"""
        if not self.reticulum:
            return []

        paths = []
        try:
            # Access path table from transport
            if hasattr(RNS.Transport, 'destination_table'):
                for destination_hash, path_data in RNS.Transport.destination_table.items():
                    path_info = {
                        "destination": RNS.prettyhexrep(destination_hash),
                        "hops": path_data[1] if len(path_data) > 1 else 0,
                        "expires": path_data[0] if len(path_data) > 0 else 0,
                        "via": RNS.prettyhexrep(path_data[2]) if len(path_data) > 2 and path_data[2] else "Local"
                    }
                    paths.append(path_info)
        except Exception as e:
            logger.error(f"Error getting path table: {e}")

        return paths

    def register_announce_callback(self, callback: Callable):
        """Register callback for announce events"""
        self.on_announce_callbacks.append(callback)

    def register_link_callback(self, callback: Callable):
        """Register callback for link events"""
        self.on_link_callbacks.append(callback)

    def create_destination(self, app_name: str, *aspects) -> RNS.Destination:
        """Create a new destination"""
        if not self.identity:
            raise RuntimeError("Identity not initialized")

        destination = RNS.Destination(
            self.identity,
            RNS.Destination.IN,
            RNS.Destination.SINGLE,
            app_name,
            *aspects
        )

        logger.info(f"Created destination: {RNS.prettyhexrep(destination.hash)}")
        return destination

    def announce_destination(self, destination: RNS.Destination, app_data: Optional[bytes] = None):
        """Announce a destination"""
        try:
            destination.announce(app_data=app_data)
            logger.info(f"Announced destination: {RNS.prettyhexrep(destination.hash)}")
        except Exception as e:
            logger.error(f"Error announcing destination: {e}")

    def establish_link(self, destination_hash: bytes) -> Optional[RNS.Link]:
        """Establish a link to a destination"""
        try:
            destination = RNS.Destination(
                None,
                RNS.Destination.OUT,
                RNS.Destination.SINGLE,
                destination_hash
            )

            link = RNS.Link(destination)
            link_id = RNS.prettyhexrep(link.link_id)
            self.active_links[link_id] = link

            logger.info(f"Establishing link to {RNS.prettyhexrep(destination_hash)}")
            return link

        except Exception as e:
            logger.error(f"Error establishing link: {e}")
            return None

    def get_uptime(self) -> float:
        """Get uptime in seconds"""
        return time.time() - self.start_time

    def get_stats(self) -> Dict:
        """Get comprehensive statistics"""
        stats = {
            "started": self.started,
            "uptime": self.get_uptime(),
            "identity": self.get_identity_info(),
            "interfaces": self.get_interface_stats(),
            "paths": len(self.get_path_table()),
            "known_destinations": len(self.known_destinations),
            "active_links": len(self.active_links),
            "transport_enabled": self.enable_transport
        }

        return stats
