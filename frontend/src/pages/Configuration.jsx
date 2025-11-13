import React, { useState, useEffect } from 'react'
import { api } from '../services/api'

function Configuration() {
  const [config, setConfig] = useState(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)
  const [saving, setSaving] = useState(false)
  const [aiModels, setAiModels] = useState([])

  useEffect(() => {
    loadConfig()
    loadAIModels()
  }, [])

  const loadConfig = async () => {
    try {
      const data = await api.getConfig()
      setConfig(data)
      setError(null)
    } catch (err) {
      setError(err.message)
    } finally {
      setLoading(false)
    }
  }

  const loadAIModels = async () => {
    try {
      const data = await api.getAIModels()
      setAiModels(data.models || [])
    } catch (err) {
      console.error('Error loading AI models:', err)
    }
  }

  const handleSave = async () => {
    setSaving(true)
    try {
      await api.updateConfig(config)
      alert('Configuration saved successfully!')
    } catch (err) {
      alert(`Error saving configuration: ${err.message}`)
    } finally {
      setSaving(false)
    }
  }

  const testNotification = async (service) => {
    try {
      const result = await api.testNotification(service)
      if (result.results[service]) {
        alert(`${service} notification sent successfully!`)
      } else {
        alert(`Failed to send ${service} notification`)
      }
    } catch (err) {
      alert(`Error testing notification: ${err.message}`)
    }
  }

  if (loading) {
    return <div className="loading">Loading configuration...</div>
  }

  if (!config) {
    return <div className="error">No configuration available</div>
  }

  return (
    <div className="configuration">
      <h2>Configuration</h2>

      {error && <div className="error">Error: {error}</div>}

      {/* LXMF Configuration */}
      <div className="config-section">
        <h3>📬 LXMF Settings</h3>
        <div className="form-group">
          <label>Display Name:</label>
          <input
            type="text"
            value={config.lxmf?.display_name || ''}
            onChange={(e) =>
              setConfig({
                ...config,
                lxmf: { ...config.lxmf, display_name: e.target.value },
              })
            }
          />
        </div>
        <div className="form-group">
          <label>Announce Interval (seconds):</label>
          <input
            type="number"
            value={config.lxmf?.announce_interval || 900}
            onChange={(e) =>
              setConfig({
                ...config,
                lxmf: { ...config.lxmf, announce_interval: parseInt(e.target.value) },
              })
            }
          />
        </div>
        <div className="form-group">
          <label>
            <input
              type="checkbox"
              checked={config.lxmf?.propagation_node || false}
              onChange={(e) =>
                setConfig({
                  ...config,
                  lxmf: { ...config.lxmf, propagation_node: e.target.checked },
                })
              }
            />
            Enable Propagation Node
          </label>
        </div>
      </div>

      {/* Command Configuration */}
      <div className="config-section">
        <h3>⚡ Command Settings</h3>
        <div className="form-group">
          <label>Command Prefix:</label>
          <input
            type="text"
            value={config.commands?.prefix || '#'}
            onChange={(e) =>
              setConfig({
                ...config,
                commands: { ...config.commands, prefix: e.target.value },
              })
            }
            maxLength="1"
          />
        </div>
      </div>

      {/* AI Configuration */}
      <div className="config-section">
        <h3>🤖 AI Settings</h3>
        <div className="form-group">
          <label>
            <input
              type="checkbox"
              checked={config.ai?.enabled || false}
              onChange={(e) =>
                setConfig({
                  ...config,
                  ai: { ...config.ai, enabled: e.target.checked },
                })
              }
            />
            Enable AI Assistant
          </label>
        </div>
        <div className="form-group">
          <label>Ollama Host:</label>
          <input
            type="text"
            value={config.ai?.ollama_host || 'http://localhost:11434'}
            onChange={(e) =>
              setConfig({
                ...config,
                ai: { ...config.ai, ollama_host: e.target.value },
              })
            }
          />
        </div>
        <div className="form-group">
          <label>Model:</label>
          <select
            value={config.ai?.model || 'llama3.2:1b'}
            onChange={(e) =>
              setConfig({
                ...config,
                ai: { ...config.ai, model: e.target.value },
              })
            }
          >
            {aiModels.map((model) => (
              <option key={model.name} value={model.name}>
                {model.name}
              </option>
            ))}
          </select>
        </div>
        <div className="form-group">
          <label>Max Response Length:</label>
          <input
            type="number"
            value={config.ai?.max_response_length || 500}
            onChange={(e) =>
              setConfig({
                ...config,
                ai: { ...config.ai, max_response_length: parseInt(e.target.value) },
              })
            }
          />
        </div>
      </div>

      {/* Email Notification Configuration */}
      <div className="config-section">
        <h3>📧 Email Notifications</h3>
        <div className="form-group">
          <label>
            <input
              type="checkbox"
              checked={config.notifications?.email?.enabled || false}
              onChange={(e) =>
                setConfig({
                  ...config,
                  notifications: {
                    ...config.notifications,
                    email: { ...config.notifications?.email, enabled: e.target.checked },
                  },
                })
              }
            />
            Enable Email Notifications
          </label>
        </div>
        <div className="form-group">
          <label>SMTP Server:</label>
          <input
            type="text"
            value={config.notifications?.email?.smtp_server || ''}
            onChange={(e) =>
              setConfig({
                ...config,
                notifications: {
                  ...config.notifications,
                  email: { ...config.notifications?.email, smtp_server: e.target.value },
                },
              })
            }
          />
        </div>
        <div className="form-group">
          <label>SMTP Port:</label>
          <input
            type="number"
            value={config.notifications?.email?.smtp_port || 587}
            onChange={(e) =>
              setConfig({
                ...config,
                notifications: {
                  ...config.notifications,
                  email: {
                    ...config.notifications?.email,
                    smtp_port: parseInt(e.target.value),
                  },
                },
              })
            }
          />
        </div>
        <div className="form-group">
          <label>Username:</label>
          <input
            type="text"
            value={config.notifications?.email?.username || ''}
            onChange={(e) =>
              setConfig({
                ...config,
                notifications: {
                  ...config.notifications,
                  email: { ...config.notifications?.email, username: e.target.value },
                },
              })
            }
          />
        </div>
        <div className="form-group">
          <label>From Address:</label>
          <input
            type="email"
            value={config.notifications?.email?.from_address || ''}
            onChange={(e) =>
              setConfig({
                ...config,
                notifications: {
                  ...config.notifications,
                  email: { ...config.notifications?.email, from_address: e.target.value },
                },
              })
            }
          />
        </div>
        <div className="form-group">
          <label>To Address:</label>
          <input
            type="email"
            value={config.notifications?.email?.to_address || ''}
            onChange={(e) =>
              setConfig({
                ...config,
                notifications: {
                  ...config.notifications,
                  email: { ...config.notifications?.email, to_address: e.target.value },
                },
              })
            }
          />
        </div>
        <button onClick={() => testNotification('email')}>Test Email</button>
      </div>

      {/* Discord Notification Configuration */}
      <div className="config-section">
        <h3>💬 Discord Notifications</h3>
        <div className="form-group">
          <label>
            <input
              type="checkbox"
              checked={config.notifications?.discord?.enabled || false}
              onChange={(e) =>
                setConfig({
                  ...config,
                  notifications: {
                    ...config.notifications,
                    discord: { ...config.notifications?.discord, enabled: e.target.checked },
                  },
                })
              }
            />
            Enable Discord Notifications
          </label>
        </div>
        <div className="form-group">
          <label>Webhook URL:</label>
          <input
            type="text"
            value={config.notifications?.discord?.webhook_url || ''}
            onChange={(e) =>
              setConfig({
                ...config,
                notifications: {
                  ...config.notifications,
                  discord: { ...config.notifications?.discord, webhook_url: e.target.value },
                },
              })
            }
          />
        </div>
        <button onClick={() => testNotification('discord')}>Test Discord</button>
      </div>

      {/* Save Button */}
      <div className="config-actions">
        <button onClick={handleSave} disabled={saving} className="primary">
          {saving ? 'Saving...' : 'Save Configuration'}
        </button>
      </div>
    </div>
  )
}

export default Configuration
