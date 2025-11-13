import React, { useState, useEffect } from 'react'
import { BrowserRouter as Router, Routes, Route, Navigate } from 'react-router-dom'
import Navigation from './components/Navigation'
import Dashboard from './pages/Dashboard'
import Messages from './pages/Messages'
import Network from './pages/Network'
import Configuration from './pages/Configuration'
import Logs from './pages/Logs'
import { WebSocketService } from './services/websocket'
import './styles/App.css'

function App() {
  const [wsConnected, setWsConnected] = useState(false)
  const [wsService, setWsService] = useState(null)

  useEffect(() => {
    // Initialize WebSocket
    const ws = new WebSocketService()
    setWsService(ws)

    ws.onConnectionChange = (connected) => {
      setWsConnected(connected)
    }

    ws.connect()

    return () => {
      ws.disconnect()
    }
  }, [])

  return (
    <Router>
      <div className="app">
        <Navigation wsConnected={wsConnected} />
        <main className="main-content">
          <Routes>
            <Route path="/" element={<Dashboard wsService={wsService} />} />
            <Route path="/messages" element={<Messages wsService={wsService} />} />
            <Route path="/network" element={<Network wsService={wsService} />} />
            <Route path="/config" element={<Configuration />} />
            <Route path="/logs" element={<Logs wsService={wsService} />} />
            <Route path="*" element={<Navigate to="/" replace />} />
          </Routes>
        </main>
      </div>
    </Router>
  )
}

export default App
