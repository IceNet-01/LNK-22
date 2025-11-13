"""AI service using Ollama for local inference"""

import requests
import logging
from typing import Optional, Dict, List

logger = logging.getLogger(__name__)


class AIService:
    """AI assistant service using Ollama"""

    def __init__(self, ollama_host: str = "http://localhost:11434",
                 model: str = "llama3.2:1b", max_response_length: int = 500,
                 system_prompt: Optional[str] = None):
        self.ollama_host = ollama_host.rstrip('/')
        self.model = model
        self.max_response_length = max_response_length
        self.system_prompt = system_prompt or (
            "You are a helpful assistant responding to queries over a mesh network. "
            "Keep responses concise and informative, suitable for limited bandwidth."
        )

        self.queries_processed = 0
        self.available_models: List[str] = []

    def query(self, prompt: str, timeout: int = 30) -> str:
        """Query the AI model"""
        try:
            logger.info(f"Processing AI query: {prompt[:50]}...")

            # Prepare request
            url = f"{self.ollama_host}/api/generate"
            payload = {
                "model": self.model,
                "prompt": prompt,
                "system": self.system_prompt,
                "stream": False,
                "options": {
                    "temperature": 0.7,
                    "num_predict": self.max_response_length
                }
            }

            # Make request
            response = requests.post(url, json=payload, timeout=timeout)
            response.raise_for_status()

            # Parse response
            result = response.json()
            answer = result.get('response', '').strip()

            # Truncate if needed
            if len(answer) > self.max_response_length:
                answer = answer[:self.max_response_length - 3] + "..."

            self.queries_processed += 1
            logger.info(f"AI query completed: {len(answer)} chars")

            return answer

        except requests.exceptions.Timeout:
            logger.error("AI query timed out")
            return "⏱️ Query timed out. Please try a simpler question."

        except requests.exceptions.ConnectionError:
            logger.error("Cannot connect to Ollama")
            return "❌ AI service unavailable. Is Ollama running?"

        except requests.exceptions.RequestException as e:
            logger.error(f"AI query error: {e}")
            return f"❌ AI error: {str(e)}"

        except Exception as e:
            logger.error(f"Unexpected error in AI query: {e}")
            return "❌ An unexpected error occurred."

    def list_models(self) -> List[Dict]:
        """List available Ollama models"""
        try:
            url = f"{self.ollama_host}/api/tags"
            response = requests.get(url, timeout=5)
            response.raise_for_status()

            result = response.json()
            models = result.get('models', [])

            self.available_models = [model['name'] for model in models]
            return models

        except Exception as e:
            logger.error(f"Error listing models: {e}")
            return []

    def check_model_exists(self, model_name: str) -> bool:
        """Check if a model exists"""
        try:
            models = self.list_models()
            return model_name in [m['name'] for m in models]
        except Exception as e:
            logger.error(f"Error checking model: {e}")
            return False

    def pull_model(self, model_name: str) -> bool:
        """Pull/download a model"""
        try:
            logger.info(f"Pulling model: {model_name}")
            url = f"{self.ollama_host}/api/pull"
            payload = {"name": model_name}

            response = requests.post(url, json=payload, timeout=300)
            response.raise_for_status()

            logger.info(f"Model pulled successfully: {model_name}")
            return True

        except Exception as e:
            logger.error(f"Error pulling model: {e}")
            return False

    def delete_model(self, model_name: str) -> bool:
        """Delete a model"""
        try:
            logger.info(f"Deleting model: {model_name}")
            url = f"{self.ollama_host}/api/delete"
            payload = {"name": model_name}

            response = requests.delete(url, json=payload, timeout=30)
            response.raise_for_status()

            logger.info(f"Model deleted successfully: {model_name}")
            return True

        except Exception as e:
            logger.error(f"Error deleting model: {e}")
            return False

    def set_model(self, model_name: str) -> bool:
        """Set the active model"""
        try:
            if self.check_model_exists(model_name):
                self.model = model_name
                logger.info(f"Active model set to: {model_name}")
                return True
            else:
                logger.warning(f"Model not found: {model_name}")
                return False
        except Exception as e:
            logger.error(f"Error setting model: {e}")
            return False

    def test_connection(self) -> bool:
        """Test connection to Ollama"""
        try:
            url = f"{self.ollama_host}/api/tags"
            response = requests.get(url, timeout=5)
            return response.status_code == 200
        except Exception:
            return False

    def get_model_info(self, model_name: Optional[str] = None) -> Optional[Dict]:
        """Get information about a specific model"""
        try:
            target_model = model_name or self.model
            url = f"{self.ollama_host}/api/show"
            payload = {"name": target_model}

            response = requests.post(url, json=payload, timeout=10)
            response.raise_for_status()

            return response.json()

        except Exception as e:
            logger.error(f"Error getting model info: {e}")
            return None

    def get_stats(self) -> Dict:
        """Get AI service statistics"""
        return {
            "ollama_host": self.ollama_host,
            "active_model": self.model,
            "max_response_length": self.max_response_length,
            "queries_processed": self.queries_processed,
            "available_models": self.available_models,
            "connected": self.test_connection()
        }
