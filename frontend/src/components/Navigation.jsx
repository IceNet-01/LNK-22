import React from 'react'
import { NavLink } from 'react-router-dom'

function Navigation({ wsConnected }) {
  return (
    <nav className="navigation">
      <div className="nav-header">
        <h1>🌐 MeshNet</h1>
        <div className={`connection-status ${wsConnected ? 'connected' : 'disconnected'}`}>
          <span className="status-dot"></span>
          {wsConnected ? 'Connected' : 'Disconnected'}
        </div>
      </div>

      <ul className="nav-links">
        <li>
          <NavLink to="/" className={({ isActive }) => isActive ? 'active' : ''}>
            📊 Dashboard
          </NavLink>
        </li>
        <li>
          <NavLink to="/messages" className={({ isActive }) => isActive ? 'active' : ''}>
            💬 Messages
          </NavLink>
        </li>
        <li>
          <NavLink to="/network" className={({ isActive }) => isActive ? 'active' : ''}>
            🌐 Network
          </NavLink>
        </li>
        <li>
          <NavLink to="/config" className={({ isActive }) => isActive ? 'active' : ''}>
            ⚙️ Configuration
          </NavLink>
        </li>
        <li>
          <NavLink to="/logs" className={({ isActive }) => isActive ? 'active' : ''}>
            📋 Logs
          </NavLink>
        </li>
      </ul>
    </nav>
  )
}

export default Navigation
