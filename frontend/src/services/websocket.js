export class WebSocketService {
  constructor() {
    this.ws = null
    this.reconnectInterval = 3000
    this.reconnectTimer = null
    this.messageCallbacks = []
    this.statusCallbacks = []
    this.announceCallbacks = []
    this.statsCallbacks = []
    this.logCallbacks = []
    this.onConnectionChange = null
  }

  connect() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
    const host = window.location.hostname
    const port = window.location.port || (protocol === 'wss:' ? 443 : 80)
    const wsUrl = `${protocol}//${host}:${port}/ws`

    console.log('Connecting to WebSocket:', wsUrl)

    try {
      this.ws = new WebSocket(wsUrl)

      this.ws.onopen = () => {
        console.log('WebSocket connected')
        if (this.reconnectTimer) {
          clearTimeout(this.reconnectTimer)
          this.reconnectTimer = null
        }
        if (this.onConnectionChange) {
          this.onConnectionChange(true)
        }
      }

      this.ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data)
          this.handleMessage(data)
        } catch (e) {
          console.error('Error parsing WebSocket message:', e)
        }
      }

      this.ws.onerror = (error) => {
        console.error('WebSocket error:', error)
      }

      this.ws.onclose = () => {
        console.log('WebSocket disconnected')
        if (this.onConnectionChange) {
          this.onConnectionChange(false)
        }
        this.scheduleReconnect()
      }
    } catch (e) {
      console.error('Error creating WebSocket:', e)
      this.scheduleReconnect()
    }
  }

  disconnect() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    if (this.ws) {
      this.ws.close()
      this.ws = null
    }
  }

  scheduleReconnect() {
    if (!this.reconnectTimer) {
      this.reconnectTimer = setTimeout(() => {
        console.log('Attempting to reconnect...')
        this.connect()
      }, this.reconnectInterval)
    }
  }

  handleMessage(data) {
    const { type, data: payload } = data

    switch (type) {
      case 'message':
        this.messageCallbacks.forEach(cb => cb(payload))
        break
      case 'status':
        this.statusCallbacks.forEach(cb => cb(payload))
        break
      case 'announce':
        this.announceCallbacks.forEach(cb => cb(payload))
        break
      case 'stats':
        this.statsCallbacks.forEach(cb => cb(payload))
        break
      case 'log':
        this.logCallbacks.forEach(cb => cb(payload))
        break
      case 'connected':
        console.log('Server confirmed connection')
        break
      default:
        console.log('Unknown message type:', type)
    }
  }

  send(data) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(data))
    } else {
      console.error('WebSocket is not connected')
    }
  }

  onMessage(callback) {
    this.messageCallbacks.push(callback)
    return () => {
      this.messageCallbacks = this.messageCallbacks.filter(cb => cb !== callback)
    }
  }

  onStatus(callback) {
    this.statusCallbacks.push(callback)
    return () => {
      this.statusCallbacks = this.statusCallbacks.filter(cb => cb !== callback)
    }
  }

  onAnnounce(callback) {
    this.announceCallbacks.push(callback)
    return () => {
      this.announceCallbacks = this.announceCallbacks.filter(cb => cb !== callback)
    }
  }

  onStats(callback) {
    this.statsCallbacks.push(callback)
    return () => {
      this.statsCallbacks = this.statsCallbacks.filter(cb => cb !== callback)
    }
  }

  onLog(callback) {
    this.logCallbacks.push(callback)
    return () => {
      this.logCallbacks = this.logCallbacks.filter(cb => cb !== callback)
    }
  }
}
