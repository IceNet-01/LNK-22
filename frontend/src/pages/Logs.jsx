import React, { useState, useEffect, useRef } from 'react'

function Logs({ wsService }) {
  const [logs, setLogs] = useState([])
  const [autoScroll, setAutoScroll] = useState(true)
  const [filter, setFilter] = useState('')
  const logsEndRef = useRef(null)

  useEffect(() => {
    if (!wsService) return

    const unsubscribe = wsService.onLog((log) => {
      setLogs((prev) => [...prev, log].slice(-500)) // Keep last 500 logs
    })

    return unsubscribe
  }, [wsService])

  useEffect(() => {
    if (autoScroll && logsEndRef.current) {
      logsEndRef.current.scrollIntoView({ behavior: 'smooth' })
    }
  }, [logs, autoScroll])

  const clearLogs = () => {
    setLogs([])
  }

  const filteredLogs = filter
    ? logs.filter((log) =>
        JSON.stringify(log).toLowerCase().includes(filter.toLowerCase())
      )
    : logs

  return (
    <div className="logs">
      <div className="logs-header">
        <h2>System Logs</h2>
        <div className="logs-controls">
          <input
            type="text"
            placeholder="Filter logs..."
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
            className="filter-input"
          />
          <label className="checkbox-label">
            <input
              type="checkbox"
              checked={autoScroll}
              onChange={(e) => setAutoScroll(e.target.checked)}
            />
            Auto-scroll
          </label>
          <button onClick={clearLogs}>Clear Logs</button>
        </div>
      </div>

      <div className="logs-content">
        {filteredLogs.length === 0 ? (
          <div className="empty-state">
            {filter ? 'No logs match your filter' : 'No logs yet. Logs will appear here as events occur.'}
          </div>
        ) : (
          <div className="logs-list">
            {filteredLogs.map((log, index) => (
              <div key={index} className={`log-entry ${log.level || 'info'}`}>
                <span className="log-timestamp">
                  {formatTimestamp(log.timestamp)}
                </span>
                <span className={`log-level ${log.level || 'info'}`}>
                  [{(log.level || 'INFO').toUpperCase()}]
                </span>
                <span className="log-message">{log.message}</span>
              </div>
            ))}
            <div ref={logsEndRef} />
          </div>
        )}
      </div>

      <div className="logs-footer">
        <span>{filteredLogs.length} log entries</span>
      </div>
    </div>
  )
}

function formatTimestamp(timestamp) {
  if (!timestamp) return new Date().toLocaleTimeString()
  const date = new Date(timestamp * 1000)
  return date.toLocaleTimeString()
}

export default Logs
