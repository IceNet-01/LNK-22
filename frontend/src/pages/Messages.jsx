import React, { useState, useEffect } from 'react'
import { api } from '../services/api'

function Messages({ wsService }) {
  const [messages, setMessages] = useState([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)
  const [sendTo, setSendTo] = useState('')
  const [sendContent, setSendContent] = useState('')
  const [sendTitle, setSendTitle] = useState('')
  const [sending, setSending] = useState(false)

  useEffect(() => {
    loadMessages()
  }, [])

  useEffect(() => {
    if (!wsService) return

    const unsubscribe = wsService.onMessage((message) => {
      setMessages((prev) => [message, ...prev])
    })

    return unsubscribe
  }, [wsService])

  const loadMessages = async () => {
    try {
      const data = await api.getMessages(100)
      setMessages(data.messages || [])
      setError(null)
    } catch (err) {
      setError(err.message)
    } finally {
      setLoading(false)
    }
  }

  const handleSend = async (e) => {
    e.preventDefault()

    if (!sendTo || !sendContent) {
      alert('Please enter destination and message content')
      return
    }

    setSending(true)

    try {
      await api.sendMessage(sendTo, sendContent, sendTitle)
      setSendContent('')
      setSendTitle('')
      alert('Message sent successfully!')
    } catch (err) {
      alert(`Error sending message: ${err.message}`)
    } finally {
      setSending(false)
    }
  }

  if (loading) {
    return <div className="loading">Loading messages...</div>
  }

  return (
    <div className="messages">
      <h2>Messages</h2>

      {/* Send Message Form */}
      <div className="send-message-section">
        <h3>Send Message</h3>
        <form onSubmit={handleSend} className="send-form">
          <div className="form-group">
            <label>Destination Hash:</label>
            <input
              type="text"
              value={sendTo}
              onChange={(e) => setSendTo(e.target.value)}
              placeholder="e.g., 1234567890abcdef..."
              disabled={sending}
            />
          </div>
          <div className="form-group">
            <label>Title (optional):</label>
            <input
              type="text"
              value={sendTitle}
              onChange={(e) => setSendTitle(e.target.value)}
              placeholder="Message title"
              disabled={sending}
            />
          </div>
          <div className="form-group">
            <label>Message:</label>
            <textarea
              value={sendContent}
              onChange={(e) => setSendContent(e.target.value)}
              placeholder="Type your message here..."
              rows="4"
              disabled={sending}
            />
          </div>
          <button type="submit" disabled={sending}>
            {sending ? 'Sending...' : 'Send Message'}
          </button>
        </form>
      </div>

      {/* Messages List */}
      <div className="messages-section">
        <h3>Message History ({messages.length})</h3>

        {error && <div className="error">Error: {error}</div>}

        {messages.length === 0 ? (
          <div className="empty-state">No messages yet</div>
        ) : (
          <div className="messages-list">
            {messages.map((msg, index) => (
              <div key={index} className={`message-item ${msg.type}`}>
                <div className="message-header">
                  <span className={`message-type ${msg.type}`}>
                    {msg.type === 'sent' ? '📤 Sent' : '📥 Received'}
                  </span>
                  <span className="message-time">
                    {formatTimestamp(msg.timestamp)}
                  </span>
                </div>

                {msg.title && (
                  <div className="message-title">{msg.title}</div>
                )}

                <div className="message-content">{msg.content}</div>

                <div className="message-footer">
                  {msg.type === 'sent' ? (
                    <>
                      <span className="message-destination">
                        To: <span className="mono">{truncateHash(msg.destination)}</span>
                      </span>
                      {msg.state && (
                        <span className={`message-state ${msg.state}`}>
                          {msg.state === 'delivered' ? '✅ Delivered' : '⏳ Pending'}
                        </span>
                      )}
                    </>
                  ) : (
                    <span className="message-source">
                      From: <span className="mono">{truncateHash(msg.source)}</span>
                    </span>
                  )}

                  {msg.rssi && (
                    <span className="message-rssi">RSSI: {msg.rssi} dBm</span>
                  )}
                  {msg.snr && (
                    <span className="message-snr">SNR: {msg.snr} dB</span>
                  )}
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}

function formatTimestamp(timestamp) {
  if (!timestamp) return 'N/A'
  const date = new Date(timestamp * 1000)
  return date.toLocaleString()
}

function truncateHash(hash) {
  if (!hash) return 'N/A'
  return hash.length > 20 ? `${hash.substring(0, 16)}...` : hash
}

export default Messages
