"""WebSocket server for real-time updates"""

import asyncio
import json
import logging
from typing import Set, Dict, Any
from aiohttp import web, WSMsgType
import aiohttp_cors

logger = logging.getLogger(__name__)


class WebSocketServer:
    """WebSocket server for real-time client updates"""

    def __init__(self, app: web.Application):
        self.app = app
        self.clients: Set[web.WebSocketResponse] = set()
        self.message_queue: asyncio.Queue = asyncio.Queue()

        # Setup routes
        self.app.router.add_get('/ws', self.websocket_handler)

    async def websocket_handler(self, request: web.Request) -> web.WebSocketResponse:
        """Handle WebSocket connections"""
        ws = web.WebSocketResponse()
        await ws.prepare(request)

        self.clients.add(ws)
        logger.info(f"WebSocket client connected. Total clients: {len(self.clients)}")

        try:
            # Send initial connection confirmation
            await self.send_to_client(ws, {
                'type': 'connected',
                'message': 'Connected to MeshNet'
            })

            # Handle messages from client
            async for msg in ws:
                if msg.type == WSMsgType.TEXT:
                    try:
                        data = json.loads(msg.data)
                        await self.handle_client_message(ws, data)
                    except json.JSONDecodeError:
                        logger.error("Invalid JSON received from client")

                elif msg.type == WSMsgType.ERROR:
                    logger.error(f"WebSocket error: {ws.exception()}")

        except Exception as e:
            logger.error(f"Error in WebSocket handler: {e}")

        finally:
            self.clients.discard(ws)
            logger.info(f"WebSocket client disconnected. Total clients: {len(self.clients)}")

        return ws

    async def handle_client_message(self, ws: web.WebSocketResponse, data: Dict[str, Any]):
        """Handle messages from clients"""
        try:
            msg_type = data.get('type')
            logger.debug(f"Received client message: {msg_type}")

            # Handle different message types
            if msg_type == 'ping':
                await self.send_to_client(ws, {'type': 'pong'})

            elif msg_type == 'subscribe':
                # Could implement topic-based subscriptions here
                await self.send_to_client(ws, {
                    'type': 'subscribed',
                    'topics': data.get('topics', [])
                })

            else:
                logger.warning(f"Unknown message type: {msg_type}")

        except Exception as e:
            logger.error(f"Error handling client message: {e}")

    async def send_to_client(self, ws: web.WebSocketResponse, data: Dict[str, Any]):
        """Send message to specific client"""
        try:
            await ws.send_json(data)
        except Exception as e:
            logger.error(f"Error sending to client: {e}")

    async def broadcast(self, data: Dict[str, Any], exclude: Set[web.WebSocketResponse] = None):
        """Broadcast message to all connected clients"""
        exclude = exclude or set()
        disconnected = set()

        for client in self.clients:
            if client in exclude:
                continue

            try:
                await client.send_json(data)
            except Exception as e:
                logger.error(f"Error broadcasting to client: {e}")
                disconnected.add(client)

        # Remove disconnected clients
        self.clients -= disconnected

    async def broadcast_message(self, message_data: Dict[str, Any]):
        """Broadcast new message to all clients"""
        await self.broadcast({
            'type': 'message',
            'data': message_data
        })

    async def broadcast_status(self, status_data: Dict[str, Any]):
        """Broadcast status update to all clients"""
        await self.broadcast({
            'type': 'status',
            'data': status_data
        })

    async def broadcast_announce(self, announce_data: Dict[str, Any]):
        """Broadcast announce event to all clients"""
        await self.broadcast({
            'type': 'announce',
            'data': announce_data
        })

    async def broadcast_stats(self, stats_data: Dict[str, Any]):
        """Broadcast statistics update to all clients"""
        await self.broadcast({
            'type': 'stats',
            'data': stats_data
        })

    async def broadcast_log(self, log_data: Dict[str, Any]):
        """Broadcast log message to all clients"""
        await self.broadcast({
            'type': 'log',
            'data': log_data
        })

    def get_client_count(self) -> int:
        """Get number of connected clients"""
        return len(self.clients)


class APIServer:
    """REST API server"""

    def __init__(self, app: web.Application, meshnet_app):
        self.app = app
        self.meshnet = meshnet_app

        # Setup routes
        self.setup_routes()

    def setup_routes(self):
        """Setup API routes"""
        # Status endpoints
        self.app.router.add_get('/api/status', self.get_status)
        self.app.router.add_get('/api/stats', self.get_stats)

        # Messages endpoints
        self.app.router.add_get('/api/messages', self.get_messages)
        self.app.router.add_post('/api/messages/send', self.send_message)

        # Network endpoints
        self.app.router.add_get('/api/network/nodes', self.get_nodes)
        self.app.router.add_get('/api/network/paths', self.get_paths)
        self.app.router.add_get('/api/network/interfaces', self.get_interfaces)

        # Identity endpoints
        self.app.router.add_get('/api/identity', self.get_identity)

        # Configuration endpoints
        self.app.router.add_get('/api/config', self.get_config)
        self.app.router.add_post('/api/config', self.update_config)

        # AI endpoints
        self.app.router.add_get('/api/ai/models', self.get_ai_models)
        self.app.router.add_post('/api/ai/query', self.ai_query)

        # Notification endpoints
        self.app.router.add_post('/api/notifications/test', self.test_notification)

    async def get_status(self, request: web.Request) -> web.Response:
        """Get system status"""
        try:
            status = self.meshnet.get_status()
            return web.json_response(status)
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def get_stats(self, request: web.Request) -> web.Response:
        """Get system statistics"""
        try:
            stats = self.meshnet.get_stats()
            return web.json_response(stats)
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def get_messages(self, request: web.Request) -> web.Response:
        """Get message history"""
        try:
            limit = request.query.get('limit', None)
            if limit:
                limit = int(limit)

            messages = self.meshnet.lxmf.get_messages(limit=limit)
            return web.json_response({'messages': messages})
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def send_message(self, request: web.Request) -> web.Response:
        """Send a message"""
        try:
            data = await request.json()
            destination = data.get('destination')
            content = data.get('content')
            title = data.get('title', '')

            if not destination or not content:
                return web.json_response({
                    'error': 'Missing destination or content'
                }, status=400)

            success = self.meshnet.lxmf.send_message(destination, content, title)

            if success:
                return web.json_response({'success': True})
            else:
                return web.json_response({'error': 'Failed to send message'}, status=500)

        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def get_nodes(self, request: web.Request) -> web.Response:
        """Get known nodes"""
        try:
            paths = self.meshnet.reticulum.get_path_table()
            return web.json_response({'nodes': paths})
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def get_paths(self, request: web.Request) -> web.Response:
        """Get routing paths"""
        try:
            paths = self.meshnet.reticulum.get_path_table()
            return web.json_response({'paths': paths})
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def get_interfaces(self, request: web.Request) -> web.Response:
        """Get interface information"""
        try:
            interfaces = self.meshnet.reticulum.get_interface_stats()
            return web.json_response({'interfaces': interfaces})
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def get_identity(self, request: web.Request) -> web.Response:
        """Get identity information"""
        try:
            identity = self.meshnet.reticulum.get_identity_info()
            return web.json_response(identity)
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def get_config(self, request: web.Request) -> web.Response:
        """Get configuration"""
        try:
            # Return safe config (no passwords)
            config = self.meshnet.config.config.copy()

            # Remove sensitive data
            if 'notifications' in config:
                if 'email' in config['notifications']:
                    config['notifications']['email']['password'] = '***'

            return web.json_response(config)
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def update_config(self, request: web.Request) -> web.Response:
        """Update configuration"""
        try:
            data = await request.json()

            # Update config
            for key, value in data.items():
                self.meshnet.config.set(key, value)

            self.meshnet.config.save()

            return web.json_response({'success': True})
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def get_ai_models(self, request: web.Request) -> web.Response:
        """Get available AI models"""
        try:
            models = self.meshnet.ai.list_models()
            return web.json_response({'models': models})
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def ai_query(self, request: web.Request) -> web.Response:
        """Query AI"""
        try:
            data = await request.json()
            query = data.get('query')

            if not query:
                return web.json_response({'error': 'Missing query'}, status=400)

            response = self.meshnet.ai.query(query)
            return web.json_response({'response': response})
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)

    async def test_notification(self, request: web.Request) -> web.Response:
        """Test notification service"""
        try:
            data = await request.json()
            service = data.get('service', 'all')

            results = {}

            if service in ['email', 'all']:
                results['email'] = self.meshnet.notifications.test_email()

            if service in ['discord', 'all']:
                results['discord'] = self.meshnet.notifications.test_discord()

            return web.json_response({'results': results})
        except Exception as e:
            return web.json_response({'error': str(e)}, status=500)


def create_app(meshnet_app) -> tuple[web.Application, WebSocketServer, APIServer]:
    """Create aiohttp application with WebSocket and API"""
    app = web.Application()

    # Setup CORS
    cors = aiohttp_cors.setup(app, defaults={
        "*": aiohttp_cors.ResourceOptions(
            allow_credentials=True,
            expose_headers="*",
            allow_headers="*",
            allow_methods="*"
        )
    })

    # Create servers
    ws_server = WebSocketServer(app)
    api_server = APIServer(app, meshnet_app)

    # Apply CORS to all routes
    for route in list(app.router.routes()):
        if not isinstance(route.resource, web.StaticResource):
            cors.add(route)

    # Serve frontend static files in production mode
    if not meshnet_app.config.get('server.dev_mode', False):
        app.router.add_static('/', path='./frontend/dist', name='static')

    return app, ws_server, api_server
