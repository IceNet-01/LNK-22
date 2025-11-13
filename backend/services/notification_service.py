"""Notification service for Email and Discord"""

import smtplib
import requests
import logging
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from typing import Dict, Optional

logger = logging.getLogger(__name__)


class EmailNotification:
    """Email notification handler"""

    def __init__(self, config: Dict):
        self.enabled = config.get('enabled', False)
        self.smtp_server = config.get('smtp_server', '')
        self.smtp_port = config.get('smtp_port', 587)
        self.use_tls = config.get('use_tls', True)
        self.username = config.get('username', '')
        self.password = config.get('password', '')
        self.from_address = config.get('from_address', '')
        self.to_address = config.get('to_address', '')

        self.emails_sent = 0

    def send(self, message: str, subject: str = "MeshNet Notification",
             source: Optional[str] = None) -> bool:
        """Send email notification"""
        if not self.enabled:
            logger.warning("Email notifications are disabled")
            return False

        try:
            logger.info(f"Sending email notification: {subject}")

            # Create message
            msg = MIMEMultipart()
            msg['From'] = self.from_address
            msg['To'] = self.to_address
            msg['Subject'] = subject

            # Build body
            body = message
            if source:
                body = f"From: {source}\n\n{message}"

            msg.attach(MIMEText(body, 'plain'))

            # Connect and send
            server = smtplib.SMTP(self.smtp_server, self.smtp_port)

            if self.use_tls:
                server.starttls()

            if self.username and self.password:
                server.login(self.username, self.password)

            server.send_message(msg)
            server.quit()

            self.emails_sent += 1
            logger.info("Email sent successfully")
            return True

        except Exception as e:
            logger.error(f"Error sending email: {e}")
            return False

    def test(self) -> bool:
        """Test email configuration"""
        return self.send("This is a test message from MeshNet.", "MeshNet Test")


class DiscordNotification:
    """Discord webhook notification handler"""

    def __init__(self, config: Dict):
        self.enabled = config.get('enabled', False)
        self.webhook_url = config.get('webhook_url', '')
        self.username = config.get('username', 'MeshNet Bot')
        self.avatar_url = config.get('avatar_url', '')

        self.messages_sent = 0

    def send(self, message: str, source: Optional[str] = None) -> bool:
        """Send Discord notification"""
        if not self.enabled:
            logger.warning("Discord notifications are disabled")
            return False

        try:
            logger.info("Sending Discord notification")

            # Build message
            content = message
            if source:
                content = f"**From:** {source}\n\n{message}"

            # Prepare payload
            payload = {
                "content": content,
                "username": self.username
            }

            if self.avatar_url:
                payload["avatar_url"] = self.avatar_url

            # Send to webhook
            response = requests.post(self.webhook_url, json=payload, timeout=10)
            response.raise_for_status()

            self.messages_sent += 1
            logger.info("Discord message sent successfully")
            return True

        except Exception as e:
            logger.error(f"Error sending Discord message: {e}")
            return False

    def test(self) -> bool:
        """Test Discord configuration"""
        return self.send("This is a test message from MeshNet.")


class NotificationService:
    """Unified notification service"""

    def __init__(self, email_config: Dict, discord_config: Dict):
        self.email = EmailNotification(email_config)
        self.discord = DiscordNotification(discord_config)

    def send_email(self, message: str, source: Optional[str] = None) -> bool:
        """Send email notification"""
        return self.email.send(message, source=source)

    def send_discord(self, message: str, source: Optional[str] = None) -> bool:
        """Send Discord notification"""
        return self.discord.send(message, source=source)

    def send_all(self, message: str, source: Optional[str] = None) -> Dict[str, bool]:
        """Send notification to all enabled channels"""
        results = {}

        if self.email.enabled:
            results['email'] = self.email.send(message, source=source)

        if self.discord.enabled:
            results['discord'] = self.discord.send(message, source=source)

        return results

    def handle_notification_command(self, command: str, message: str,
                                   source: Optional[str] = None) -> bool:
        """Handle notification command from command handler"""
        try:
            if command == 'email':
                return self.send_email(message, source)
            elif command == 'discord':
                return self.send_discord(message, source)
            elif command == 'notify':
                results = self.send_all(message, source)
                return any(results.values())  # True if any notification succeeded
            else:
                logger.warning(f"Unknown notification command: {command}")
                return False

        except Exception as e:
            logger.error(f"Error handling notification command: {e}")
            return False

    def test_email(self) -> bool:
        """Test email configuration"""
        return self.email.test()

    def test_discord(self) -> bool:
        """Test Discord configuration"""
        return self.discord.test()

    def get_stats(self) -> Dict:
        """Get notification statistics"""
        return {
            "email": {
                "enabled": self.email.enabled,
                "sent": self.email.emails_sent,
                "configured": bool(self.email.smtp_server and self.email.from_address)
            },
            "discord": {
                "enabled": self.discord.enabled,
                "sent": self.discord.messages_sent,
                "configured": bool(self.discord.webhook_url)
            }
        }
