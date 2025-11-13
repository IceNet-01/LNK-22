"""Configuration management for MeshNet"""

import json
import os
from pathlib import Path
from typing import Any, Dict
import logging

logger = logging.getLogger(__name__)


class Config:
    """Configuration manager for MeshNet"""

    def __init__(self, config_path: str = "config.json"):
        self.config_path = config_path
        self.config: Dict[str, Any] = {}
        self.load()

    def load(self):
        """Load configuration from file"""
        try:
            if os.path.exists(self.config_path):
                with open(self.config_path, 'r') as f:
                    self.config = json.load(f)
                logger.info(f"Configuration loaded from {self.config_path}")
            else:
                # Load from example config
                example_path = "config.example.json"
                if os.path.exists(example_path):
                    with open(example_path, 'r') as f:
                        self.config = json.load(f)
                    logger.warning(f"No config.json found, loaded from {example_path}")
                else:
                    logger.error("No configuration file found")
                    self.config = self._default_config()
        except Exception as e:
            logger.error(f"Error loading configuration: {e}")
            self.config = self._default_config()

    def save(self):
        """Save configuration to file"""
        try:
            with open(self.config_path, 'w') as f:
                json.dump(self.config, f, indent=2)
            logger.info(f"Configuration saved to {self.config_path}")
        except Exception as e:
            logger.error(f"Error saving configuration: {e}")

    def get(self, key: str, default: Any = None) -> Any:
        """Get configuration value by dot-notation key"""
        keys = key.split('.')
        value = self.config
        for k in keys:
            if isinstance(value, dict) and k in value:
                value = value[k]
            else:
                return default
        return value

    def set(self, key: str, value: Any):
        """Set configuration value by dot-notation key"""
        keys = key.split('.')
        config = self.config
        for k in keys[:-1]:
            if k not in config:
                config[k] = {}
            config = config[k]
        config[keys[-1]] = value

    def _default_config(self) -> Dict[str, Any]:
        """Return default configuration"""
        return {
            "reticulum": {
                "config_path": "~/.reticulum",
                "enable_transport": True,
                "enable_propagation": False,
                "data_dir": "./data"
            },
            "lxmf": {
                "display_name": "MeshNet Node",
                "announce_interval": 900,
                "propagation_node": False,
                "delivery_confirmations": True
            },
            "server": {
                "host": "0.0.0.0",
                "port": 8080,
                "dev_mode": False
            },
            "commands": {
                "prefix": "#",
                "rate_limit": {
                    "commands_per_minute": 10,
                    "ai_queries_per_minute": 3
                }
            },
            "ai": {
                "enabled": True,
                "ollama_host": "http://localhost:11434",
                "model": "llama3.2:1b",
                "max_response_length": 500
            },
            "notifications": {
                "email": {"enabled": False},
                "discord": {"enabled": False}
            }
        }

    def ensure_directories(self):
        """Ensure required directories exist"""
        dirs = [
            self.get('reticulum.data_dir', './data'),
            os.path.dirname(self.get('logging.file', './logs/meshnet.log')),
            os.path.dirname(self.get('storage.database_path', './data/meshnet.db'))
        ]

        for dir_path in dirs:
            if dir_path:
                Path(dir_path).mkdir(parents=True, exist_ok=True)
                logger.debug(f"Ensured directory exists: {dir_path}")


# Global config instance
config = Config()
