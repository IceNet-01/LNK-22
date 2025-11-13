"""Command handler for processing special commands"""

import time
import logging
from typing import Dict, Optional, Callable, List
from datetime import datetime, timedelta

logger = logging.getLogger(__name__)


class RateLimiter:
    """Rate limiting for commands"""

    def __init__(self, max_commands: int = 10, window_seconds: int = 60):
        self.max_commands = max_commands
        self.window_seconds = window_seconds
        self.command_history: Dict[str, List[float]] = {}

    def check_rate_limit(self, user_id: str) -> bool:
        """Check if user is within rate limit"""
        now = time.time()

        # Initialize user history if not exists
        if user_id not in self.command_history:
            self.command_history[user_id] = []

        # Remove old commands outside window
        self.command_history[user_id] = [
            cmd_time for cmd_time in self.command_history[user_id]
            if now - cmd_time < self.window_seconds
        ]

        # Check if under limit
        if len(self.command_history[user_id]) >= self.max_commands:
            return False

        # Add current command
        self.command_history[user_id].append(now)
        return True

    def get_remaining(self, user_id: str) -> int:
        """Get remaining commands for user"""
        if user_id not in self.command_history:
            return self.max_commands

        now = time.time()
        recent_commands = [
            cmd_time for cmd_time in self.command_history[user_id]
            if now - cmd_time < self.window_seconds
        ]

        return max(0, self.max_commands - len(recent_commands))


class CommandHandler:
    """Handler for special commands in messages"""

    def __init__(self, prefix: str = "#", enabled_commands: Optional[List[str]] = None):
        self.prefix = prefix
        self.enabled_commands = enabled_commands or []

        # Rate limiters
        self.command_rate_limiter = RateLimiter(max_commands=10, window_seconds=60)
        self.ai_rate_limiter = RateLimiter(max_commands=3, window_seconds=60)

        # Command statistics
        self.commands_processed = 0
        self.commands_by_type: Dict[str, int] = {}

        # External service handlers
        self.ai_handler: Optional[Callable] = None
        self.notification_handler: Optional[Callable] = None
        self.reticulum_core = None
        self.lxmf_core = None

        # System start time
        self.start_time = time.time()

    def set_ai_handler(self, handler: Callable):
        """Set AI query handler"""
        self.ai_handler = handler

    def set_notification_handler(self, handler: Callable):
        """Set notification handler"""
        self.notification_handler = handler

    def set_reticulum_core(self, core):
        """Set Reticulum core reference"""
        self.reticulum_core = core

    def set_lxmf_core(self, core):
        """Set LXMF core reference"""
        self.lxmf_core = core

    def is_command(self, content: str) -> bool:
        """Check if message is a command"""
        return content.strip().startswith(self.prefix)

    def process_command(self, content: str, source: str) -> Optional[str]:
        """Process command and return response"""
        try:
            if not self.is_command(content):
                return None

            # Remove prefix and parse
            command_text = content.strip()[len(self.prefix):].strip()
            parts = command_text.split(maxsplit=1)
            command = parts[0].lower()
            args = parts[1] if len(parts) > 1 else ""

            # Check rate limit
            if not self.command_rate_limiter.check_rate_limit(source):
                return "⚠️ Rate limit exceeded. Please wait before sending more commands."

            # Check if command is enabled
            if self.enabled_commands and command not in self.enabled_commands:
                return f"❌ Command '{command}' is not enabled."

            # Track statistics
            self.commands_processed += 1
            self.commands_by_type[command] = self.commands_by_type.get(command, 0) + 1

            # Route to appropriate handler
            if command in ['ai', 'ask']:
                return self._handle_ai_command(args, source)
            elif command in ['email', 'discord', 'notify']:
                return self._handle_notification_command(command, args, source)
            else:
                return self._handle_info_command(command, args, source)

        except Exception as e:
            logger.error(f"Error processing command: {e}")
            return f"❌ Error processing command: {str(e)}"

    def _handle_info_command(self, command: str, args: str, source: str) -> str:
        """Handle information commands"""
        try:
            if command == "ping":
                return "🏓 Pong!"

            elif command == "help":
                return self._get_help_text()

            elif command == "status":
                return self._get_status()

            elif command == "time":
                return f"⏰ {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"

            elif command == "uptime":
                return self._get_uptime()

            elif command == "version":
                return "🔧 MeshNet v1.0.0 - Reticulum Network System"

            elif command == "nodes":
                return self._get_nodes()

            elif command == "paths":
                return self._get_paths()

            elif command == "interfaces":
                return self._get_interfaces()

            elif command == "stats":
                return self._get_stats()

            elif command == "identity":
                return self._get_identity()

            elif command == "announces":
                return "📡 Announce monitoring is active"

            else:
                return f"❓ Unknown command: {command}. Type #help for available commands."

        except Exception as e:
            logger.error(f"Error handling info command: {e}")
            return f"❌ Error: {str(e)}"

    def _handle_ai_command(self, query: str, source: str) -> str:
        """Handle AI query command"""
        try:
            # Check AI rate limit
            if not self.ai_rate_limiter.check_rate_limit(source):
                remaining = self.command_rate_limiter.get_remaining(source)
                return f"⚠️ AI rate limit exceeded. {remaining} general commands remaining."

            if not query:
                return "❓ Please provide a question. Usage: #ai <question>"

            if not self.ai_handler:
                return "❌ AI service is not available."

            # Call AI handler
            response = self.ai_handler(query)
            return f"🤖 {response}"

        except Exception as e:
            logger.error(f"Error handling AI command: {e}")
            return f"❌ AI Error: {str(e)}"

    def _handle_notification_command(self, command: str, message: str, source: str) -> str:
        """Handle notification commands"""
        try:
            if not message:
                return f"❓ Please provide a message. Usage: #{command} <message>"

            if not self.notification_handler:
                return "❌ Notification service is not available."

            # Call notification handler
            result = self.notification_handler(command, message, source)

            if result:
                return f"✅ Notification sent via {command}"
            else:
                return f"❌ Failed to send notification via {command}"

        except Exception as e:
            logger.error(f"Error handling notification command: {e}")
            return f"❌ Notification Error: {str(e)}"

    def _get_help_text(self) -> str:
        """Get help text"""
        help_text = "📋 Available Commands:\n\n"
        help_text += "ℹ️ Information:\n"
        help_text += "  #ping - Test connectivity\n"
        help_text += "  #status - System status\n"
        help_text += "  #time - Current time\n"
        help_text += "  #uptime - System uptime\n"
        help_text += "  #version - Version info\n\n"
        help_text += "🌐 Network:\n"
        help_text += "  #nodes - Known nodes\n"
        help_text += "  #paths - Routing paths\n"
        help_text += "  #interfaces - RNS interfaces\n\n"
        help_text += "🤖 AI:\n"
        help_text += "  #ai <query> - Ask AI\n\n"
        help_text += "📢 Notifications:\n"
        help_text += "  #email <msg> - Send email\n"
        help_text += "  #discord <msg> - Send to Discord\n"
        help_text += "  #notify <msg> - Send all notifications"
        return help_text

    def _get_status(self) -> str:
        """Get system status"""
        status = "📊 System Status:\n\n"

        if self.reticulum_core and self.reticulum_core.started:
            status += f"✅ Reticulum: Online\n"
            status += f"🔑 Identity: {self.reticulum_core.get_identity_info().get('hash', 'N/A')[:16]}...\n"
        else:
            status += "❌ Reticulum: Offline\n"

        if self.lxmf_core and self.lxmf_core.started:
            status += f"✅ LXMF: Online\n"
            status += f"📬 Messages: {self.lxmf_core.messages_received}↓ {self.lxmf_core.messages_sent}↑\n"
        else:
            status += "❌ LXMF: Offline\n"

        status += f"⚡ Commands: {self.commands_processed}"

        return status

    def _get_uptime(self) -> str:
        """Get system uptime"""
        uptime_seconds = time.time() - self.start_time
        uptime_str = str(timedelta(seconds=int(uptime_seconds)))
        return f"⏱️ Uptime: {uptime_str}"

    def _get_nodes(self) -> str:
        """Get known nodes"""
        if not self.reticulum_core:
            return "❌ Reticulum not available"

        paths = self.reticulum_core.get_path_table()
        if not paths:
            return "📡 No known nodes yet"

        node_list = f"📡 Known Nodes ({len(paths)}):\n\n"
        for i, path in enumerate(paths[:5], 1):  # Limit to 5 for brevity
            node_list += f"{i}. {path['destination'][:16]}... ({path['hops']} hops)\n"

        if len(paths) > 5:
            node_list += f"\n...and {len(paths) - 5} more"

        return node_list

    def _get_paths(self) -> str:
        """Get routing paths"""
        if not self.reticulum_core:
            return "❌ Reticulum not available"

        paths = self.reticulum_core.get_path_table()
        if not paths:
            return "🛤️ No paths in table"

        path_list = f"🛤️ Path Table ({len(paths)}):\n\n"
        for i, path in enumerate(paths[:3], 1):  # Limit to 3 for brevity
            path_list += f"{i}. {path['destination'][:16]}...\n"
            path_list += f"   Hops: {path['hops']}, Via: {path['via'][:16]}...\n"

        if len(paths) > 3:
            path_list += f"\n...and {len(paths) - 3} more"

        return path_list

    def _get_interfaces(self) -> str:
        """Get interface information"""
        if not self.reticulum_core:
            return "❌ Reticulum not available"

        interfaces = self.reticulum_core.get_interface_stats()
        if not interfaces:
            return "🔌 No interfaces configured"

        if_list = f"🔌 Interfaces ({len(interfaces)}):\n\n"
        for i, iface in enumerate(interfaces, 1):
            status = "✅" if iface.get('online', False) else "❌"
            if_list += f"{i}. {status} {iface['name']}\n"
            if_list += f"   RX: {iface['rxb']} | TX: {iface['txb']}\n"

        return if_list

    def _get_stats(self) -> str:
        """Get comprehensive statistics"""
        stats = "📈 Statistics:\n\n"

        if self.reticulum_core:
            rns_stats = self.reticulum_core.get_stats()
            stats += f"🌐 RNS:\n"
            stats += f"  Interfaces: {len(rns_stats.get('interfaces', []))}\n"
            stats += f"  Paths: {rns_stats.get('paths', 0)}\n"
            stats += f"  Links: {rns_stats.get('active_links', 0)}\n\n"

        if self.lxmf_core:
            lxmf_stats = self.lxmf_core.get_stats()
            stats += f"📬 LXMF:\n"
            stats += f"  Received: {lxmf_stats.get('messages_received', 0)}\n"
            stats += f"  Sent: {lxmf_stats.get('messages_sent', 0)}\n\n"

        stats += f"⚡ Commands:\n"
        stats += f"  Processed: {self.commands_processed}\n"

        return stats

    def _get_identity(self) -> str:
        """Get identity information"""
        if not self.reticulum_core:
            return "❌ Reticulum not available"

        identity_info = self.reticulum_core.get_identity_info()
        if not identity_info:
            return "❌ No identity available"

        info = "🔑 Identity:\n\n"
        info += f"Hash: {identity_info.get('hash', 'N/A')}\n"
        info += f"Can Sign: {'✅' if identity_info.get('can_sign', False) else '❌'}\n"
        info += f"Can Encrypt: {'✅' if identity_info.get('can_encrypt', False) else '❌'}"

        return info

    def get_stats(self) -> Dict:
        """Get command handler statistics"""
        return {
            "commands_processed": self.commands_processed,
            "commands_by_type": self.commands_by_type,
            "prefix": self.prefix,
            "enabled_commands": self.enabled_commands
        }
