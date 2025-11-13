import React, { useState, useEffect } from 'react'
import { api } from '../services/api'

function Network({ wsService }) {
  const [nodes, setNodes] = useState([])
  const [paths, setPaths] = useState([])
  const [interfaces, setInterfaces] = useState([])
  const [identity, setIdentity] = useState(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)
  const [activeTab, setActiveTab] = useState('nodes')

  useEffect(() => {
    loadData()
    const interval = setInterval(loadData, 10000) // Refresh every 10 seconds
    return () => clearInterval(interval)
  }, [])

  useEffect(() => {
    if (!wsService) return

    const unsubscribeAnnounce = wsService.onAnnounce((data) => {
      // Handle new announces
      loadData()
    })

    return unsubscribeAnnounce
  }, [wsService])

  const loadData = async () => {
    try {
      const [nodesData, pathsData, interfacesData, identityData] = await Promise.all([
        api.getNodes(),
        api.getPaths(),
        api.getInterfaces(),
        api.getIdentity(),
      ])

      setNodes(nodesData.nodes || [])
      setPaths(pathsData.paths || [])
      setInterfaces(interfacesData.interfaces || [])
      setIdentity(identityData)
      setError(null)
    } catch (err) {
      setError(err.message)
    } finally {
      setLoading(false)
    }
  }

  if (loading) {
    return <div className="loading">Loading network data...</div>
  }

  return (
    <div className="network">
      <h2>Network</h2>

      {/* Identity Section */}
      {identity && (
        <div className="identity-section">
          <h3>🔑 Local Identity</h3>
          <div className="identity-info">
            <div className="info-item">
              <span className="info-label">Hash:</span>
              <span className="info-value mono">{identity.hash}</span>
            </div>
            <div className="info-item">
              <span className="info-label">Capabilities:</span>
              <span className="info-value">
                {identity.can_sign && '✅ Sign '}
                {identity.can_encrypt && '✅ Encrypt'}
              </span>
            </div>
          </div>
        </div>
      )}

      {/* Tabs */}
      <div className="tabs">
        <button
          className={activeTab === 'nodes' ? 'active' : ''}
          onClick={() => setActiveTab('nodes')}
        >
          📡 Nodes ({nodes.length})
        </button>
        <button
          className={activeTab === 'paths' ? 'active' : ''}
          onClick={() => setActiveTab('paths')}
        >
          🛤️ Paths ({paths.length})
        </button>
        <button
          className={activeTab === 'interfaces' ? 'active' : ''}
          onClick={() => setActiveTab('interfaces')}
        >
          🔌 Interfaces ({interfaces.length})
        </button>
      </div>

      {error && <div className="error">Error: {error}</div>}

      {/* Tab Content */}
      <div className="tab-content">
        {activeTab === 'nodes' && (
          <div className="nodes-tab">
            {nodes.length === 0 ? (
              <div className="empty-state">No known nodes yet</div>
            ) : (
              <div className="nodes-list">
                {nodes.map((node, index) => (
                  <div key={index} className="node-item">
                    <div className="node-header">
                      <span className="node-destination mono">{node.destination}</span>
                      <span className="node-hops">{node.hops} hops</span>
                    </div>
                    <div className="node-footer">
                      <span className="node-via">Via: {node.via}</span>
                      {node.expires && (
                        <span className="node-expires">
                          Expires: {formatExpiry(node.expires)}
                        </span>
                      )}
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}

        {activeTab === 'paths' && (
          <div className="paths-tab">
            {paths.length === 0 ? (
              <div className="empty-state">No paths in table</div>
            ) : (
              <div className="paths-list">
                {paths.map((path, index) => (
                  <div key={index} className="path-item">
                    <div className="path-header">
                      <span className="path-destination mono">{path.destination}</span>
                      <span className="path-hops">{path.hops} hops</span>
                    </div>
                    <div className="path-footer">
                      <span className="path-via">Via: {path.via}</span>
                      {path.expires && (
                        <span className="path-expires">
                          Expires: {formatExpiry(path.expires)}
                        </span>
                      )}
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}

        {activeTab === 'interfaces' && (
          <div className="interfaces-tab">
            {interfaces.length === 0 ? (
              <div className="empty-state">No interfaces configured</div>
            ) : (
              <div className="interfaces-list">
                {interfaces.map((iface, index) => (
                  <div key={index} className="interface-card">
                    <div className="interface-header">
                      <span className={`status-indicator ${iface.online ? 'online' : 'offline'}`}>
                        {iface.online ? '🟢' : '🔴'}
                      </span>
                      <span className="interface-name">{iface.name}</span>
                      <span className="interface-mode">{iface.mode}</span>
                    </div>
                    <div className="interface-stats">
                      <div className="stat-item">
                        <span className="stat-label">Received:</span>
                        <span className="stat-value">{formatBytes(iface.rxb)}</span>
                      </div>
                      <div className="stat-item">
                        <span className="stat-label">Transmitted:</span>
                        <span className="stat-value">{formatBytes(iface.txb)}</span>
                      </div>
                      {iface.bitrate && (
                        <div className="stat-item">
                          <span className="stat-label">Bitrate:</span>
                          <span className="stat-value">{iface.bitrate} bps</span>
                        </div>
                      )}
                    </div>
                    {iface.status && (
                      <div className="interface-status">{iface.status}</div>
                    )}
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  )
}

function formatExpiry(timestamp) {
  if (!timestamp) return 'N/A'
  const now = Date.now() / 1000
  const remaining = timestamp - now

  if (remaining < 0) return 'Expired'

  const hours = Math.floor(remaining / 3600)
  const minutes = Math.floor((remaining % 3600) / 60)

  return `${hours}h ${minutes}m`
}

function formatBytes(bytes) {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i]
}

export default Network
