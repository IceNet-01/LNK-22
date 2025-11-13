#!/usr/bin/env python3
"""
MeshNet - Reticulum Network System
Main application entry point
"""

import asyncio
import argparse
import logging
import signal
import sys
import os
from pathlib import Path
from aiohttp import web

# Add backend to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'backend'))

from utils.config import config
from core.reticulum_core import ReticulumCore
from core.lxmf_core import LXMFCore
from core.command_handler import CommandHandler
from services.ai_service import AIService
from services.notification_service import NotificationService
from server.websocket_server import create_app

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class MeshNet:
    """Main MeshNet application"""

    def __init__(self, config_path: str = "config.json"):
        logger.info("Initializing MeshNet...")

        # Load configuration
        self.config = config
        if config_path != "config.json":
            self.config.config_path = config_path
            self.config.load()

        # Ensure directories exist
        self.config.ensure_directories()

        # Initialize components
        self.reticulum: ReticulumCore = None
        self.lxmf: LXMFCore = None
        self.command_handler: CommandHandler = None
        self.ai: AIService = None
        self.notifications: NotificationService = None

        # Web server components
        self.app: web.Application = None
        self.ws_server = None
        self.api_server = None
        self.web_runner: web.AppRunner = None

        # Running state
        self.running = False

    def initialize(self):
        """Initialize all components"""
        try:
            logger.info("Starting component initialization...")

            # Initialize Reticulum
            self.reticulum = ReticulumCore(
                config_path=self.config.get('reticulum.config_path'),
                enable_transport=self.config.get('reticulum.enable_transport', True)
            )
            self.reticulum.start()

            # Initialize LXMF
            self.lxmf = LXMFCore(
                reticulum_core=self.reticulum,
                display_name=self.config.get('lxmf.display_name', 'MeshNet Node'),
                announce_interval=self.config.get('lxmf.announce_interval', 900),
                enable_propagation=self.config.get('lxmf.propagation_node', False)
            )
            self.lxmf.start()

            # Initialize AI service
            ai_config = self.config.get('ai', {})
            self.ai = AIService(
                ollama_host=ai_config.get('ollama_host', 'http://localhost:11434'),
                model=ai_config.get('model', 'llama3.2:1b'),
                max_response_length=ai_config.get('max_response_length', 500),
                system_prompt=ai_config.get('system_prompt')
            )

            # Initialize notification service
            self.notifications = NotificationService(
                email_config=self.config.get('notifications.email', {}),
                discord_config=self.config.get('notifications.discord', {})
            )

            # Initialize command handler
            self.command_handler = CommandHandler(
                prefix=self.config.get('commands.prefix', '#'),
                enabled_commands=self.config.get('commands.enabled_commands')
            )

            # Wire up command handler
            self.command_handler.set_reticulum_core(self.reticulum)
            self.command_handler.set_lxmf_core(self.lxmf)
            self.command_handler.set_ai_handler(self._handle_ai_query)
            self.command_handler.set_notification_handler(self._handle_notification)

            # Register LXMF message callback
            self.lxmf.register_message_callback(self._on_lxmf_message)

            logger.info("All components initialized successfully")

        except Exception as e:
            logger.error(f"Error initializing components: {e}")
            raise

    def _on_lxmf_message(self, message_data: dict):
        """Handle incoming LXMF messages"""
        try:
            content = message_data.get('content', '')
            source = message_data.get('source', '')

            logger.info(f"Processing message from {source}")

            # Check if it's a command
            if self.command_handler.is_command(content):
                response = self.command_handler.process_command(content, source)

                if response:
                    # Send response back to sender
                    self.lxmf.send_message(
                        destination_hash=source,
                        content=response,
                        title="Command Response"
                    )

            # Broadcast to web clients
            if self.ws_server:
                asyncio.create_task(self.ws_server.broadcast_message(message_data))

        except Exception as e:
            logger.error(f"Error handling LXMF message: {e}")

    def _handle_ai_query(self, query: str) -> str:
        """Handle AI query from command handler"""
        try:
            return self.ai.query(query)
        except Exception as e:
            logger.error(f"Error in AI query: {e}")
            return f"Error: {str(e)}"

    def _handle_notification(self, command: str, message: str, source: str) -> bool:
        """Handle notification from command handler"""
        try:
            return self.notifications.handle_notification_command(command, message, source)
        except Exception as e:
            logger.error(f"Error in notification: {e}")
            return False

    async def start_web_server(self):
        """Start web server"""
        try:
            host = self.config.get('server.host', '0.0.0.0')
            port = self.config.get('server.port', 8080)

            logger.info(f"Starting web server on {host}:{port}...")

            # Create aiohttp app
            self.app, self.ws_server, self.api_server = create_app(self)

            # Setup and start runner
            self.web_runner = web.AppRunner(self.app)
            await self.web_runner.setup()

            site = web.TCPSite(self.web_runner, host, port)
            await site.start()

            logger.info(f"Web server started on http://{host}:{port}")

            if self.config.get('server.dev_mode', False):
                logger.info(f"Frontend dev server: http://localhost:{self.config.get('server.frontend_dev_port', 5173)}")
            else:
                logger.info("Open your browser to access the Web UI")

        except Exception as e:
            logger.error(f"Error starting web server: {e}")
            raise

    async def stop_web_server(self):
        """Stop web server"""
        try:
            if self.web_runner:
                await self.web_runner.cleanup()
                logger.info("Web server stopped")
        except Exception as e:
            logger.error(f"Error stopping web server: {e}")

    def stop(self):
        """Stop all components"""
        try:
            logger.info("Stopping MeshNet...")

            if self.lxmf:
                self.lxmf.stop()

            if self.reticulum:
                self.reticulum.stop()

            self.running = False
            logger.info("MeshNet stopped")

        except Exception as e:
            logger.error(f"Error stopping MeshNet: {e}")

    async def run(self):
        """Run the application"""
        try:
            self.running = True

            # Initialize components
            self.initialize()

            # Start web server
            await self.start_web_server()

            logger.info("MeshNet is running. Press Ctrl+C to stop.")

            # Keep running
            while self.running:
                await asyncio.sleep(1)

                # Periodic announce (if configured)
                # This would be handled by LXMF's internal timer

        except Exception as e:
            logger.error(f"Error in main loop: {e}")
            raise

        finally:
            await self.stop_web_server()
            self.stop()

    def get_status(self) -> dict:
        """Get system status"""
        return {
            "running": self.running,
            "reticulum": self.reticulum.get_stats() if self.reticulum else {},
            "lxmf": self.lxmf.get_stats() if self.lxmf else {},
            "ws_clients": self.ws_server.get_client_count() if self.ws_server else 0
        }

    def get_stats(self) -> dict:
        """Get comprehensive statistics"""
        stats = {
            "reticulum": self.reticulum.get_stats() if self.reticulum else {},
            "lxmf": self.lxmf.get_stats() if self.lxmf else {},
            "commands": self.command_handler.get_stats() if self.command_handler else {},
            "ai": self.ai.get_stats() if self.ai else {},
            "notifications": self.notifications.get_stats() if self.notifications else {},
            "websocket": {
                "clients": self.ws_server.get_client_count() if self.ws_server else 0
            }
        }
        return stats


def signal_handler(signum, frame):
    """Handle shutdown signals"""
    logger.info("Received shutdown signal")
    sys.exit(0)


async def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='MeshNet - Reticulum Network System')
    parser.add_argument('--config', '-c', default='config.json',
                       help='Path to configuration file')
    parser.add_argument('--dev', action='store_true',
                       help='Run in development mode')

    args = parser.parse_args()

    # Set dev mode if specified
    if args.dev:
        config.set('server.dev_mode', True)

    # Setup signal handlers
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Create and run application
    app = MeshNet(config_path=args.config)

    try:
        await app.run()
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    except Exception as e:
        logger.error(f"Fatal error: {e}")
        sys.exit(1)


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Shutting down...")
