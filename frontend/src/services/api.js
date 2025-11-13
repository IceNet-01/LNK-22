const API_BASE = '/api'

async function fetchAPI(endpoint, options = {}) {
  const response = await fetch(`${API_BASE}${endpoint}`, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...options.headers,
    },
  })

  if (!response.ok) {
    const error = await response.json().catch(() => ({ error: 'Request failed' }))
    throw new Error(error.error || `HTTP ${response.status}`)
  }

  return response.json()
}

export const api = {
  // Status
  getStatus: () => fetchAPI('/status'),
  getStats: () => fetchAPI('/stats'),

  // Messages
  getMessages: (limit) => fetchAPI(`/messages${limit ? `?limit=${limit}` : ''}`),
  sendMessage: (destination, content, title = '') =>
    fetchAPI('/messages/send', {
      method: 'POST',
      body: JSON.stringify({ destination, content, title }),
    }),

  // Network
  getNodes: () => fetchAPI('/network/nodes'),
  getPaths: () => fetchAPI('/network/paths'),
  getInterfaces: () => fetchAPI('/network/interfaces'),

  // Identity
  getIdentity: () => fetchAPI('/identity'),

  // Configuration
  getConfig: () => fetchAPI('/config'),
  updateConfig: (config) =>
    fetchAPI('/config', {
      method: 'POST',
      body: JSON.stringify(config),
    }),

  // AI
  getAIModels: () => fetchAPI('/ai/models'),
  queryAI: (query) =>
    fetchAPI('/ai/query', {
      method: 'POST',
      body: JSON.stringify({ query }),
    }),

  // Notifications
  testNotification: (service = 'all') =>
    fetchAPI('/notifications/test', {
      method: 'POST',
      body: JSON.stringify({ service }),
    }),
}
