import React, { useState, useEffect } from 'react'
import { api } from '../services/api'

function Dashboard({ wsService }) {
  const [status, setStatus] = useState(null)
  const [stats, setStats] = useState(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)

  useEffect(() => {
    loadData()
    const interval = setInterval(loadData, 5000) // Refresh every 5 seconds
    return () => clearInterval(interval)
  }, [])

  useEffect(() => {
    if (!wsService) return

    const unsubscribeStats = wsService.onStats((data) => {
      setStats(data)
    })

    const unsubscribeStatus = wsService.onStatus((data) => {
      setStatus((prev) => ({ ...prev, ...data }))
    })

    return () => {
      unsubscribeStats()
      unsubscribeStatus()
    }
  }, [wsService])

  const loadData = async () => {
    try {
      const [statusData, statsData] = await Promise.all([
        api.getStatus(),
        api.getStats(),
      ])
      setStatus(statusData)
      setStats(statsData)
      setError(null)
    } catch (err) {
      setError(err.message)
    } finally {
      setLoading(false)
    }
  }

  if (loading) {
    return <div className="loading">Loading dashboard...</div>
  }

  if (error) {
    return <div className="error">Error: {error}</div>
  }

  const rnsStats = stats?.reticulum || {}
  const lxmfStats = stats?.lxmf || {}
  const commandStats = stats?.commands || {}

  return (
    <div className="dashboard">
      <h2>Dashboard</h2>

      <div className="stats-grid">
        {/* Reticulum Status */}
        <div className="stat-card">
          <h3>🔷 Reticulum</h3>
          <div className="stat-content">
            <div className="stat-item">
              <span className="stat-label">Status:</span>
              <span className={`stat-value ${rnsStats.started ? 'success' : 'error'}`}>
                {rnsStats.started ? 'Online' : 'Offline'}
              </span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Uptime:</span>
              <span className="stat-value">
                {rnsStats.uptime ? formatUptime(rnsStats.uptime) : 'N/A'}
              </span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Interfaces:</span>
              <span className="stat-value">{rnsStats.interfaces?.length || 0}</span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Known Paths:</span>
              <span className="stat-value">{rnsStats.paths || 0}</span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Active Links:</span>
              <span className="stat-value">{rnsStats.active_links || 0}</span>
            </div>
          </div>
        </div>

        {/* LXMF Status */}
        <div className="stat-card">
          <h3>📬 LXMF Messaging</h3>
          <div className="stat-content">
            <div className="stat-item">
              <span className="stat-label">Status:</span>
              <span className={`stat-value ${lxmfStats.started ? 'success' : 'error'}`}>
                {lxmfStats.started ? 'Online' : 'Offline'}
              </span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Display Name:</span>
              <span className="stat-value">{lxmfStats.display_name || 'N/A'}</span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Messages Received:</span>
              <span className="stat-value">{lxmfStats.messages_received || 0}</span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Messages Sent:</span>
              <span className="stat-value">{lxmfStats.messages_sent || 0}</span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Destination:</span>
              <span className="stat-value mono">
                {lxmfStats.destination ? truncateHash(lxmfStats.destination) : 'N/A'}
              </span>
            </div>
          </div>
        </div>

        {/* Commands */}
        <div className="stat-card">
          <h3>⚡ Commands</h3>
          <div className="stat-content">
            <div className="stat-item">
              <span className="stat-label">Processed:</span>
              <span className="stat-value">{commandStats.commands_processed || 0}</span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Prefix:</span>
              <span className="stat-value">{commandStats.prefix || '#'}</span>
            </div>
          </div>
        </div>

        {/* Identity */}
        <div className="stat-card">
          <h3>🔑 Identity</h3>
          <div className="stat-content">
            <div className="stat-item">
              <span className="stat-label">Hash:</span>
              <span className="stat-value mono">
                {rnsStats.identity?.hash ? truncateHash(rnsStats.identity.hash) : 'N/A'}
              </span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Can Sign:</span>
              <span className="stat-value">
                {rnsStats.identity?.can_sign ? '✅ Yes' : '❌ No'}
              </span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Can Encrypt:</span>
              <span className="stat-value">
                {rnsStats.identity?.can_encrypt ? '✅ Yes' : '❌ No'}
              </span>
            </div>
          </div>
        </div>
      </div>

      {/* Interfaces */}
      {rnsStats.interfaces && rnsStats.interfaces.length > 0 && (
        <div className="section">
          <h3>🔌 Interfaces</h3>
          <div className="interfaces-list">
            {rnsStats.interfaces.map((iface, index) => (
              <div key={index} className="interface-item">
                <div className="interface-header">
                  <span className={`status-indicator ${iface.online ? 'online' : 'offline'}`}>
                    {iface.online ? '🟢' : '🔴'}
                  </span>
                  <span className="interface-name">{iface.name}</span>
                  <span className="interface-mode">{iface.mode}</span>
                </div>
                <div className="interface-stats">
                  <span>RX: {formatBytes(iface.rxb)}</span>
                  <span>TX: {formatBytes(iface.txb)}</span>
                </div>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}

function formatUptime(seconds) {
  const hours = Math.floor(seconds / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)
  const secs = Math.floor(seconds % 60)
  return `${hours}h ${minutes}m ${secs}s`
}

function formatBytes(bytes) {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i]
}

function truncateHash(hash) {
  if (!hash) return 'N/A'
  return hash.length > 20 ? `${hash.substring(0, 16)}...` : hash
}

export default Dashboard
