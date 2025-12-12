/**
 * LNK-22 Enhanced Web Client v1.8.0
 * Full-featured mesh network monitoring and control interface
 * Supports all firmware features: Links, Groups, DTN, Emergency, ADR, Store-Forward
 */

// =============================================================================
// Version Information
// =============================================================================

const WEB_CLIENT_VERSION = '1.9.1';
const MIN_FIRMWARE_VERSION = '1.8.0';

// =============================================================================
// Global State Management
// =============================================================================

const state = {
    port: null,
    reader: null,
    writer: null,
    connected: false,

    // Version tracking
    firmwareVersion: null,
    versionMismatch: false,

    // Network data
    nodeAddress: null,
    nodeName: null,
    nodeNames: new Map(),
    neighbors: new Map(),
    routes: new Map(),
    messages: [],

    // Chat state (Meshtastic-style)
    currentChat: 'broadcast',  // 'broadcast' or node name/address
    conversations: new Map(),  // Map of chatId -> { messages: [], unread: 0 }
    unreadCounts: new Map(),   // Map of chatId -> unread count

    // Feature data
    links: {
        active: 0,
        max: 8,
        list: new Map()
    },
    groups: {
        count: 0,
        max: 8,
        list: new Map(),
        stats: { sent: 0, received: 0, decrypted: 0, failed: 0, replay: 0 }
    },
    storeForward: {
        queued: 0,
        max: 32,
        delivered: 0,
        expired: 0,
        pending: []
    },
    adr: {
        enabled: true,
        defaultSF: 10,
        sfChanges: 0,
        packetsBySF: { 7: 0, 8: 0, 9: 0, 10: 0, 11: 0, 12: 0 }
    },
    emergency: {
        active: false,
        type: null
    },
    dtn: {
        bundles: [],
        stats: { created: 0, delivered: 0, pending: 0 }
    },
    geo: {
        enabled: false,
        mode: 'off',
        nodes: 0
    },

    // Statistics
    stats: {
        packetsReceived: 0,
        packetsSent: 0,
        uptime: 0,
        rssi: 0,
        snr: 0,
        channel: 0
    },

    // GPS data
    gps: {
        latitude: null,
        longitude: null,
        altitude: null,
        satellites: 0,
        fix: false
    },

    // Chart instances
    charts: {
        packetActivity: null,
        signalQuality: null
    },

    // Console history
    consoleHistory: [],
    historyIndex: -1,

    // Parsing state
    currentSection: null
};

// =============================================================================
// Serial Port Management
// =============================================================================

async function connectSerial() {
    try {
        // Clean up any existing connection first
        if (state.connected) {
            console.log('[WEB] Disconnecting existing connection first');
            await disconnectSerial();
        }

        console.log('[WEB] Requesting serial port...');
        state.port = await navigator.serial.requestPort();
        console.log('[WEB] Got port, opening at 115200 baud...');
        await state.port.open({ baudRate: 115200 });
        console.log('[WEB] Port opened successfully');

        state.reader = state.port.readable.getReader();
        state.writer = state.port.writable.getWriter();
        state.connected = true;

        console.log('[WEB] Reader and writer acquired, connected:', state.connected);

        updateConnectionStatus(true);
        addConsoleMessage('Connected to radio', 'success');
        showToast('Connected to radio', 'success');

        // Start reading FIRST before sending commands
        console.log('[WEB] Starting read loop...');
        readSerialData();

        // Query ALL feature statuses on connect (with small delay to let read loop start)
        setTimeout(() => {
            console.log('[WEB] Querying status...');
            queryAllStatus();
        }, 300);

    } catch (error) {
        console.error('[WEB] Connection error:', error);
        const errorMsg = error.message || String(error);

        if (errorMsg.includes('cancelled') || errorMsg.includes('canceled')) {
            // User cancelled the port selection dialog
            addConsoleMessage('Connection cancelled', 'info');
        } else {
            addConsoleMessage(`Connection failed: ${errorMsg}`, 'error');
            showToast('Failed to connect to radio', 'error');
        }
    }
}

// Polling interval ID
let statusPollInterval = null;

function queryAllStatus() {
    // Basic status
    sendCommand('status');

    // After a delay, query all features
    setTimeout(() => sendCommand('name'), 200);
    setTimeout(() => sendCommand('name list'), 400);
    setTimeout(() => sendCommand('neighbors'), 600);
    setTimeout(() => sendCommand('routes'), 800);
    setTimeout(() => sendCommand('radio'), 1000);
    setTimeout(() => sendCommand('mac'), 1200);  // MAC/TDMA status

    // Advanced features
    setTimeout(() => sendCommand('link'), 1400);
    setTimeout(() => sendCommand('group'), 1600);
    setTimeout(() => sendCommand('queue'), 1800);
    setTimeout(() => sendCommand('adr'), 2000);
    setTimeout(() => sendCommand('history'), 2200);

    // Start periodic polling for live updates
    startStatusPolling();
}

function startStatusPolling() {
    // Clear any existing interval
    if (statusPollInterval) {
        clearInterval(statusPollInterval);
    }

    // Poll every 5 seconds for live updates
    statusPollInterval = setInterval(() => {
        if (state.connected) {
            // Rotate through status queries to avoid flooding
            const pollCommands = ['neighbors', 'mac', 'status'];
            const cmd = pollCommands[Math.floor(Date.now() / 5000) % pollCommands.length];
            sendCommand(cmd);
        } else {
            // Stop polling if disconnected
            clearInterval(statusPollInterval);
            statusPollInterval = null;
        }
    }, 5000);
}

function stopStatusPolling() {
    if (statusPollInterval) {
        clearInterval(statusPollInterval);
        statusPollInterval = null;
    }
}

async function disconnectSerial() {
    try {
        // Stop polling
        stopStatusPolling();

        if (state.reader) {
            await state.reader.cancel();
            state.reader.releaseLock();
        }
        if (state.writer) {
            state.writer.releaseLock();
        }
        if (state.port) {
            await state.port.close();
        }

        state.port = null;
        state.reader = null;
        state.writer = null;
        state.connected = false;

        updateConnectionStatus(false);
        addConsoleMessage('Disconnected from radio', 'info');

    } catch (error) {
        console.error('Disconnect error:', error);
    }
}

async function readSerialData() {
    const decoder = new TextDecoder();
    let buffer = '';

    console.log('[WEB] Starting serial read loop, connected:', state.connected, 'reader:', !!state.reader);
    addConsoleMessage('Serial read loop started', 'info');

    try {
        while (state.connected && state.reader) {
            const { value, done } = await state.reader.read();

            if (done) {
                console.log('[WEB] Serial read done signal received');
                addConsoleMessage('Serial read stream ended', 'warning');
                break;
            }

            if (value && value.length > 0) {
                const chunk = decoder.decode(value, { stream: true });
                buffer += chunk;
                console.log(`[WEB] Received ${value.length} bytes: "${chunk.substring(0, 50)}..."`);

                let newlineIndex;
                while ((newlineIndex = buffer.indexOf('\n')) >= 0) {
                    const line = buffer.slice(0, newlineIndex).trim();
                    buffer = buffer.slice(newlineIndex + 1);

                    if (line) {
                        processSerialLine(line);
                    }
                }
            }
        }
        console.log('[WEB] Read loop exited - connected:', state.connected, 'reader:', !!state.reader);
    } catch (error) {
        console.error('[WEB] Read error:', error);
        const errorMsg = error.message || String(error);

        // Handle "device has been lost" gracefully
        if (errorMsg.includes('device has been lost') || errorMsg.includes('disconnected')) {
            addConsoleMessage('Device disconnected - please reconnect', 'warning');
            showToast('Device disconnected', 'warning');
        } else if (errorMsg.includes('cancelled') || errorMsg.includes('canceled')) {
            addConsoleMessage('Serial read cancelled', 'info');
        } else {
            addConsoleMessage(`Read error: ${errorMsg}`, 'error');
        }

        await disconnectSerial();
    }
}

async function sendCommand(command) {
    if (!state.writer) return;

    try {
        const encoder = new TextEncoder();
        await state.writer.write(encoder.encode(command + '\n'));
        addConsoleMessage(`> ${command}`, 'command');
    } catch (error) {
        console.error('Send error:', error);
        addConsoleMessage(`Send error: ${error.message}`, 'error');
    }
}

// =============================================================================
// Web Bluetooth API - BLE Mesh Node Support
// =============================================================================

// LNK-22 BLE Service UUIDs (must match firmware)
// Nordic UART Service (fallback for serial-style communication)
const LNK22_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';  // Nordic UART
const LNK22_TX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';       // Write to device
const LNK22_RX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';       // Read from device

// LNK-22 Custom Service UUIDs (for direct GATT access)
const LNK22_CUSTOM_SERVICE = '4c4e0001-4b32-1000-8000-00805f9b34fb';  // LNK22 Main Service
const LNK22_MSG_RX_UUID = '4c4e0002-4b32-1000-8000-00805f9b34fb';     // Message write
const LNK22_MSG_TX_UUID = '4c4e0003-4b32-1000-8000-00805f9b34fb';     // Message notify
const LNK22_STATUS_UUID = '4c4e0005-4b32-1000-8000-00805f9b34fb';     // Status read/notify
const LNK22_RELAY_UUID = '4c4e000b-4b32-1000-8000-00805f9b34fb';      // Mesh relay characteristic

// Relay message types (must match firmware)
const RELAY_MSG = {
    DATA: 0x01,
    BEACON: 0x02,
    ROUTE_REQ: 0x03,
    ROUTE_REP: 0x04,
    ACK: 0x05,
    REGISTER: 0x10,
    UNREGISTER: 0x11,
    HEARTBEAT: 0x12,
    STATUS: 0x20
};

// BLE connection state (separate from serial)
const bleState = {
    device: null,
    server: null,
    service: null,
    txCharacteristic: null,
    rxCharacteristic: null,
    relayCharacteristic: null,
    connected: false,
    relayMode: false,
    virtualAddr: null,
    peers: new Map()  // Track BLE mesh peers
};

// Check if Web Bluetooth is available
function isBLEAvailable() {
    return navigator.bluetooth !== undefined;
}

// Scan for and connect to a BLE device
async function connectBLE() {
    if (!isBLEAvailable()) {
        showToast('Web Bluetooth not supported in this browser', 'error');
        addConsoleMessage('Web Bluetooth API not available. Use Chrome/Edge on desktop or enable experimental flags.', 'error');
        return;
    }

    try {
        addConsoleMessage('Scanning for LNK-22 BLE devices...', 'info');

        // Request device with LNK-22 service
        bleState.device = await navigator.bluetooth.requestDevice({
            filters: [
                { services: [LNK22_SERVICE_UUID] },
                { namePrefix: 'LNK-22' },
                { namePrefix: 'LNK22' }
            ],
            optionalServices: [LNK22_SERVICE_UUID]
        });

        if (!bleState.device) {
            addConsoleMessage('No device selected', 'info');
            return;
        }

        addConsoleMessage(`Connecting to ${bleState.device.name || 'BLE device'}...`, 'info');

        // Set up disconnect handler
        bleState.device.addEventListener('gattserverdisconnected', onBLEDisconnected);

        // Connect to GATT server
        bleState.server = await bleState.device.gatt.connect();
        addConsoleMessage('GATT server connected', 'success');

        // Get LNK-22 service
        bleState.service = await bleState.server.getPrimaryService(LNK22_SERVICE_UUID);

        // Get TX characteristic (for sending commands)
        bleState.txCharacteristic = await bleState.service.getCharacteristic(LNK22_TX_UUID);

        // Get RX characteristic (for receiving data)
        bleState.rxCharacteristic = await bleState.service.getCharacteristic(LNK22_RX_UUID);

        // Start notifications for incoming data
        await bleState.rxCharacteristic.startNotifications();
        bleState.rxCharacteristic.addEventListener('characteristicvaluechanged', onBLEDataReceived);

        bleState.connected = true;

        addConsoleMessage(`Connected to ${bleState.device.name || 'BLE device'} via Bluetooth`, 'success');
        showToast(`BLE connected: ${bleState.device.name || 'device'}`, 'success');

        updateBLEStatus();

        // Query device status
        setTimeout(() => {
            sendBLECommand('status');
            sendBLECommand('name');
        }, 500);

    } catch (error) {
        console.error('[BLE] Connection error:', error);
        const errorMsg = error.message || String(error);

        if (errorMsg.includes('cancelled') || errorMsg.includes('canceled')) {
            addConsoleMessage('BLE scan cancelled', 'info');
        } else if (errorMsg.includes('Bluetooth adapter not available')) {
            addConsoleMessage('Bluetooth not available. Enable Bluetooth on your device.', 'error');
            showToast('Enable Bluetooth', 'error');
        } else {
            addConsoleMessage(`BLE connection failed: ${errorMsg}`, 'error');
            showToast('BLE connection failed', 'error');
        }
    }
}

// Disconnect from BLE device
async function disconnectBLE() {
    try {
        if (bleState.rxCharacteristic) {
            await bleState.rxCharacteristic.stopNotifications();
        }

        if (bleState.device && bleState.device.gatt.connected) {
            bleState.device.gatt.disconnect();
        }
    } catch (error) {
        console.error('[BLE] Disconnect error:', error);
    }

    resetBLEState();
    addConsoleMessage('BLE disconnected', 'info');
    updateBLEStatus();
}

// Handle BLE disconnection
function onBLEDisconnected() {
    addConsoleMessage('BLE device disconnected', 'warning');
    showToast('BLE device disconnected', 'warning');
    resetBLEState();
    updateBLEStatus();
}

// Reset BLE state
function resetBLEState() {
    bleState.device = null;
    bleState.server = null;
    bleState.service = null;
    bleState.txCharacteristic = null;
    bleState.rxCharacteristic = null;
    bleState.relayCharacteristic = null;
    bleState.connected = false;
    bleState.relayMode = false;
    bleState.virtualAddr = null;
}

// =============================================================================
// BLE Mesh Relay Mode
// =============================================================================

// Enable relay mode - allows this web client to act as a mesh node
async function enableBLERelayMode(clientName = 'WebClient') {
    if (!bleState.connected) {
        addConsoleMessage('Must connect to BLE device first', 'error');
        return false;
    }

    try {
        // Try to get the relay characteristic
        try {
            bleState.relayCharacteristic = await bleState.service.getCharacteristic(LNK22_RELAY_UUID);
        } catch (e) {
            // Relay characteristic may not exist on all firmware versions
            addConsoleMessage('Relay mode not supported by this firmware', 'warning');
            return false;
        }

        // Subscribe to relay notifications
        await bleState.relayCharacteristic.startNotifications();
        bleState.relayCharacteristic.addEventListener('characteristicvaluechanged', onBLERelayData);

        // Register as a relay client
        const nameBytes = new TextEncoder().encode(clientName);
        const registerMsg = new Uint8Array(3 + nameBytes.length);
        registerMsg[0] = RELAY_MSG.REGISTER;  // Message type
        registerMsg[1] = 1;  // relay_enabled = true
        registerMsg[2] = nameBytes.length;  // name length
        registerMsg.set(nameBytes, 3);

        await bleState.relayCharacteristic.writeValue(registerMsg);

        bleState.relayMode = true;
        addConsoleMessage(`Relay mode enabled as "${clientName}"`, 'success');
        showToast('BLE Relay Mode Active', 'success');

        // Start heartbeat
        startRelayHeartbeat();

        return true;
    } catch (error) {
        console.error('[BLE-RELAY] Enable error:', error);
        addConsoleMessage(`Relay mode error: ${error.message}`, 'error');
        return false;
    }
}

// Disable relay mode
async function disableBLERelayMode() {
    if (!bleState.relayMode || !bleState.relayCharacteristic) {
        return;
    }

    try {
        // Send unregister message
        const unregisterMsg = new Uint8Array([RELAY_MSG.UNREGISTER]);
        await bleState.relayCharacteristic.writeValue(unregisterMsg);

        // Stop notifications
        await bleState.relayCharacteristic.stopNotifications();
        bleState.relayCharacteristic.removeEventListener('characteristicvaluechanged', onBLERelayData);

        bleState.relayMode = false;
        bleState.virtualAddr = null;

        addConsoleMessage('Relay mode disabled', 'info');
    } catch (error) {
        console.error('[BLE-RELAY] Disable error:', error);
    }
}

// Handle incoming relay data from firmware
function onBLERelayData(event) {
    const value = event.target.value;
    const data = new Uint8Array(value.buffer);

    if (data.length < 1) return;

    const msgType = data[0];

    switch (msgType) {
        case RELAY_MSG.STATUS: {
            // Registration response: [type:1][virtualAddr:4][nodeAddr:4]
            if (data.length >= 9) {
                bleState.virtualAddr = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
                const nodeAddr = data[5] | (data[6] << 8) | (data[7] << 16) | (data[8] << 24);
                addConsoleMessage(`Relay registered: Virtual 0x${bleState.virtualAddr.toString(16).toUpperCase()} via Node 0x${nodeAddr.toString(16).toUpperCase()}`, 'success');
            }
            break;
        }

        case RELAY_MSG.DATA: {
            // Mesh message: [type:1][rssi:2][snr:1][packet_header+payload]
            if (data.length >= 4) {
                const rssi = data[1] | (data[2] << 8);
                const snr = data[3];
                const packetData = data.slice(4);

                // Parse packet header (simplified - adjust based on actual header format)
                if (packetData.length >= 24) {  // Minimum header size
                    const source = packetData[10] | (packetData[11] << 8) | (packetData[12] << 16) | (packetData[13] << 24);
                    const dest = packetData[14] | (packetData[15] << 8) | (packetData[16] << 16) | (packetData[17] << 24);
                    const payloadLen = packetData[22] | (packetData[23] << 8);
                    const payload = packetData.slice(24, 24 + payloadLen);

                    // Decode payload as text
                    const text = new TextDecoder().decode(payload);

                    addConsoleMessage(`[RELAY] Message from 0x${source.toString(16).toUpperCase()}: ${text} (RSSI: ${rssi}, SNR: ${snr})`, 'output');

                    // Also display in chat
                    if (text.trim()) {
                        addReceivedMessage(source, dest, text, false);
                    }
                }
            }
            break;
        }

        case RELAY_MSG.BEACON: {
            addConsoleMessage('[RELAY] Beacon received', 'output');
            break;
        }

        default:
            console.log('[BLE-RELAY] Unknown message type:', msgType);
    }
}

// Send message via relay
async function sendRelayMessage(dest, message) {
    if (!bleState.relayMode || !bleState.relayCharacteristic) {
        addConsoleMessage('Relay mode not active', 'error');
        return false;
    }

    try {
        const msgBytes = new TextEncoder().encode(message);

        // Format: [type:1][dest:4][flags:1][payload]
        const relayMsg = new Uint8Array(6 + msgBytes.length);
        relayMsg[0] = RELAY_MSG.DATA;
        relayMsg[1] = dest & 0xFF;
        relayMsg[2] = (dest >> 8) & 0xFF;
        relayMsg[3] = (dest >> 16) & 0xFF;
        relayMsg[4] = (dest >> 24) & 0xFF;
        relayMsg[5] = 0;  // flags
        relayMsg.set(msgBytes, 6);

        await bleState.relayCharacteristic.writeValue(relayMsg);

        addConsoleMessage(`[RELAY] Sent to 0x${dest.toString(16).toUpperCase()}: ${message}`, 'command');
        return true;
    } catch (error) {
        console.error('[BLE-RELAY] Send error:', error);
        addConsoleMessage(`Relay send error: ${error.message}`, 'error');
        return false;
    }
}

// Heartbeat to keep relay connection alive
let relayHeartbeatInterval = null;

function startRelayHeartbeat() {
    if (relayHeartbeatInterval) {
        clearInterval(relayHeartbeatInterval);
    }

    relayHeartbeatInterval = setInterval(async () => {
        if (bleState.relayMode && bleState.relayCharacteristic) {
            try {
                const heartbeat = new Uint8Array([RELAY_MSG.HEARTBEAT]);
                await bleState.relayCharacteristic.writeValue(heartbeat);
            } catch (error) {
                console.warn('[BLE-RELAY] Heartbeat failed:', error);
            }
        }
    }, 30000);  // Every 30 seconds
}

function stopRelayHeartbeat() {
    if (relayHeartbeatInterval) {
        clearInterval(relayHeartbeatInterval);
        relayHeartbeatInterval = null;
    }
}

// Handle incoming BLE data
function onBLEDataReceived(event) {
    const value = event.target.value;
    const decoder = new TextDecoder();
    const data = decoder.decode(value);

    // Process each line (BLE data might come in chunks)
    const lines = data.split('\n');
    for (const line of lines) {
        if (line.trim()) {
            // Add BLE prefix to distinguish from serial
            addConsoleMessage(`[BLE] ${line.trim()}`, 'output');
            // Process the line same as serial data
            processSerialLine(line.trim());
        }
    }
}

// Send command over BLE
async function sendBLECommand(command) {
    if (!bleState.connected || !bleState.txCharacteristic) {
        console.warn('[BLE] Not connected');
        return false;
    }

    try {
        const encoder = new TextEncoder();
        const data = encoder.encode(command + '\n');

        // BLE has max packet size, chunk if needed
        const chunkSize = 20;  // BLE MTU is typically 20-512 bytes
        for (let i = 0; i < data.length; i += chunkSize) {
            const chunk = data.slice(i, Math.min(i + chunkSize, data.length));
            await bleState.txCharacteristic.writeValue(chunk);
        }

        addConsoleMessage(`[BLE] > ${command}`, 'command');
        return true;
    } catch (error) {
        console.error('[BLE] Send error:', error);
        addConsoleMessage(`[BLE] Send error: ${error.message}`, 'error');
        return false;
    }
}

// Update BLE status indicator in UI
function updateBLEStatus() {
    const bleStatusEl = document.getElementById('bleStatus');
    const bleDeviceName = document.getElementById('bleDeviceName');
    const bleConnectBtn = document.getElementById('bleConnectBtn');
    const relayModeBtn = document.getElementById('relayModeBtn');
    const relayStatus = document.getElementById('relayStatus');
    const relayVirtualAddr = document.getElementById('relayVirtualAddr');

    if (bleStatusEl) {
        bleStatusEl.className = bleState.connected ? 'ble-status connected' : 'ble-status disconnected';
        bleStatusEl.textContent = bleState.connected ? 'BLE Connected' : 'BLE Disconnected';
    }

    if (bleDeviceName) {
        bleDeviceName.textContent = bleState.connected && bleState.device
            ? bleState.device.name || 'Unknown Device'
            : '-';
    }

    if (bleConnectBtn) {
        bleConnectBtn.textContent = bleState.connected ? 'Disconnect BLE' : 'Connect BLE';
        bleConnectBtn.className = bleState.connected ? 'btn btn-danger' : 'btn btn-primary';
    }

    // Update relay mode UI
    if (relayModeBtn) {
        relayModeBtn.disabled = !bleState.connected;
        relayModeBtn.textContent = bleState.relayMode ? 'Disable Relay Mode' : 'Enable Relay Mode';
        relayModeBtn.className = bleState.relayMode ? 'btn btn-success' : 'btn btn-secondary';
    }

    if (relayStatus) {
        relayStatus.textContent = bleState.relayMode ? 'Relay Active' : 'Relay Off';
        relayStatus.style.background = bleState.relayMode ? '#4CAF50' : '#333';
        relayStatus.style.color = bleState.relayMode ? '#fff' : '#888';
    }

    if (relayVirtualAddr) {
        if (bleState.virtualAddr) {
            relayVirtualAddr.textContent = `Virtual: 0x${bleState.virtualAddr.toString(16).toUpperCase()}`;
        } else {
            relayVirtualAddr.textContent = '';
        }
    }
}

// Toggle relay mode on/off
async function toggleRelayMode() {
    if (!bleState.connected) {
        showToast('Connect to BLE device first', 'warning');
        return;
    }

    if (bleState.relayMode) {
        await disableBLERelayMode();
    } else {
        const clientName = document.getElementById('relayClientName')?.value || 'WebClient';
        await enableBLERelayMode(clientName);
    }

    updateBLEStatus();
}

// Scan for nearby BLE mesh peers (discovery)
async function scanBLEPeers() {
    if (!isBLEAvailable()) {
        showToast('Web Bluetooth not supported', 'error');
        return;
    }

    try {
        addConsoleMessage('Scanning for BLE mesh peers...', 'info');

        // Use requestDevice to find nearby LNK-22 devices
        // This is a simplified scan - full mesh would use background scanning
        const device = await navigator.bluetooth.requestDevice({
            filters: [
                { namePrefix: 'LNK-22' },
                { namePrefix: 'LNK22' }
            ],
            optionalServices: [LNK22_SERVICE_UUID]
        });

        if (device) {
            const peerId = device.id || device.name;
            bleState.peers.set(peerId, {
                device: device,
                name: device.name,
                lastSeen: Date.now()
            });

            addConsoleMessage(`Found BLE peer: ${device.name}`, 'success');
            updateBLEPeersList();
        }

    } catch (error) {
        if (!error.message.includes('cancelled')) {
            addConsoleMessage(`BLE scan error: ${error.message}`, 'error');
        }
    }
}

// Update BLE peers list in UI
function updateBLEPeersList() {
    const peersList = document.getElementById('blePeersList');
    if (!peersList) return;

    if (bleState.peers.size === 0) {
        peersList.innerHTML = '<p class="text-muted">No BLE peers discovered</p>';
        return;
    }

    let html = '<div class="ble-peers">';
    for (const [id, peer] of bleState.peers) {
        html += `
            <div class="ble-peer-item">
                <span class="peer-icon">📱</span>
                <span class="peer-name">${escapeHtml(peer.name || 'Unknown')}</span>
                <button class="btn btn-small btn-primary" onclick="connectToBLEPeer('${id}')">Connect</button>
            </div>
        `;
    }
    html += '</div>';
    peersList.innerHTML = html;
}

// Connect to a specific BLE peer
async function connectToBLEPeer(peerId) {
    const peer = bleState.peers.get(peerId);
    if (!peer || !peer.device) {
        showToast('Peer not found', 'error');
        return;
    }

    try {
        addConsoleMessage(`Connecting to BLE peer: ${peer.name}...`, 'info');

        // If we have a current connection, disconnect first
        if (bleState.connected) {
            await disconnectBLE();
        }

        // Connect to the peer
        bleState.device = peer.device;
        bleState.device.addEventListener('gattserverdisconnected', onBLEDisconnected);

        bleState.server = await bleState.device.gatt.connect();
        bleState.service = await bleState.server.getPrimaryService(LNK22_SERVICE_UUID);
        bleState.txCharacteristic = await bleState.service.getCharacteristic(LNK22_TX_UUID);
        bleState.rxCharacteristic = await bleState.service.getCharacteristic(LNK22_RX_UUID);

        await bleState.rxCharacteristic.startNotifications();
        bleState.rxCharacteristic.addEventListener('characteristicvaluechanged', onBLEDataReceived);

        bleState.connected = true;

        addConsoleMessage(`Connected to BLE peer: ${peer.name}`, 'success');
        showToast(`Connected to ${peer.name}`, 'success');
        updateBLEStatus();

    } catch (error) {
        addConsoleMessage(`Failed to connect to peer: ${error.message}`, 'error');
        showToast('Connection failed', 'error');
    }
}

// =============================================================================
// WebRTC LAN Discovery - Peer-to-Peer Mesh on Local Network
// =============================================================================

// WebRTC configuration for local network discovery
const rtcConfig = {
    iceServers: [
        { urls: 'stun:stun.l.google.com:19302' },
        { urls: 'stun:stun1.l.google.com:19302' }
    ]
};

// LAN mesh state
const lanState = {
    localId: null,
    peers: new Map(),  // peerId -> { connection, channel, name, lastSeen }
    discoveryServer: null,
    discoverySocket: null,
    isDiscovering: false,
    announceInterval: null
};

// Generate unique local ID
function generateLocalId() {
    if (!lanState.localId) {
        lanState.localId = 'web-' + Math.random().toString(36).substring(2, 10);
    }
    return lanState.localId;
}

// Initialize LAN discovery
function initLANDiscovery() {
    generateLocalId();
    addConsoleMessage(`[LAN] Local ID: ${lanState.localId}`, 'info');

    // Check WebRTC support
    if (!window.RTCPeerConnection) {
        addConsoleMessage('[LAN] WebRTC not supported in this browser', 'error');
        return false;
    }

    return true;
}

// Start WebSocket signaling server connection (for discovery)
async function startLANDiscovery(serverUrl = null) {
    if (lanState.isDiscovering) {
        addConsoleMessage('[LAN] Discovery already active', 'warning');
        return;
    }

    if (!initLANDiscovery()) return;

    // Default to local signaling server
    const signalingUrl = serverUrl || `ws://${window.location.hostname}:8765`;

    try {
        addConsoleMessage(`[LAN] Connecting to signaling server: ${signalingUrl}`, 'info');

        lanState.discoverySocket = new WebSocket(signalingUrl);

        lanState.discoverySocket.onopen = () => {
            addConsoleMessage('[LAN] Connected to signaling server', 'success');
            lanState.isDiscovering = true;

            // Announce ourselves
            announceLANPresence();

            // Set up periodic announcements
            lanState.announceInterval = setInterval(announceLANPresence, 30000);

            updateLANStatus();
        };

        lanState.discoverySocket.onmessage = async (event) => {
            try {
                const message = JSON.parse(event.data);
                await handleSignalingMessage(message);
            } catch (error) {
                console.error('[LAN] Failed to parse signaling message:', error);
            }
        };

        lanState.discoverySocket.onclose = () => {
            addConsoleMessage('[LAN] Signaling server disconnected', 'warning');
            stopLANDiscovery();
        };

        lanState.discoverySocket.onerror = (error) => {
            addConsoleMessage('[LAN] Signaling server error - is server running?', 'error');
            console.error('[LAN] WebSocket error:', error);
        };

    } catch (error) {
        addConsoleMessage(`[LAN] Failed to start discovery: ${error.message}`, 'error');
    }
}

// Stop LAN discovery
function stopLANDiscovery() {
    if (lanState.announceInterval) {
        clearInterval(lanState.announceInterval);
        lanState.announceInterval = null;
    }

    if (lanState.discoverySocket) {
        lanState.discoverySocket.close();
        lanState.discoverySocket = null;
    }

    // Close all peer connections
    for (const [peerId, peer] of lanState.peers) {
        if (peer.connection) {
            peer.connection.close();
        }
    }
    lanState.peers.clear();

    lanState.isDiscovering = false;
    addConsoleMessage('[LAN] Discovery stopped', 'info');
    updateLANStatus();
    updateLANPeersList();
}

// Announce presence on LAN
function announceLANPresence() {
    if (!lanState.discoverySocket || lanState.discoverySocket.readyState !== WebSocket.OPEN) {
        return;
    }

    const nodeName = state.nodeName || ('Web-' + lanState.localId.slice(-4).toUpperCase());

    lanState.discoverySocket.send(JSON.stringify({
        type: 'announce',
        from: lanState.localId,
        name: nodeName,
        timestamp: Date.now()
    }));
}

// Handle signaling messages
async function handleSignalingMessage(message) {
    const { type, from, to } = message;

    // Ignore messages from ourselves
    if (from === lanState.localId) return;

    // Ignore messages not intended for us (except broadcasts)
    if (to && to !== lanState.localId) return;

    switch (type) {
        case 'announce':
            // New peer announced
            if (!lanState.peers.has(from)) {
                addConsoleMessage(`[LAN] Discovered peer: ${message.name || from}`, 'success');

                // Create peer entry
                lanState.peers.set(from, {
                    connection: null,
                    channel: null,
                    name: message.name || from,
                    lastSeen: Date.now()
                });

                // Initiate connection
                await connectToLANPeer(from);
            } else {
                // Update last seen
                const peer = lanState.peers.get(from);
                peer.lastSeen = Date.now();
                peer.name = message.name || peer.name;
            }
            updateLANPeersList();
            break;

        case 'offer':
            await handleRTCOffer(from, message.sdp);
            break;

        case 'answer':
            await handleRTCAnswer(from, message.sdp);
            break;

        case 'ice-candidate':
            await handleICECandidate(from, message.candidate);
            break;

        case 'leave':
            if (lanState.peers.has(from)) {
                const peer = lanState.peers.get(from);
                if (peer.connection) peer.connection.close();
                lanState.peers.delete(from);
                addConsoleMessage(`[LAN] Peer left: ${peer.name}`, 'info');
                updateLANPeersList();
            }
            break;
    }
}

// Create WebRTC connection to a peer
async function connectToLANPeer(peerId) {
    const peer = lanState.peers.get(peerId);
    if (!peer) return;

    try {
        // Create RTCPeerConnection
        const pc = new RTCPeerConnection(rtcConfig);
        peer.connection = pc;

        // Handle ICE candidates
        pc.onicecandidate = (event) => {
            if (event.candidate) {
                sendSignalingMessage({
                    type: 'ice-candidate',
                    to: peerId,
                    candidate: event.candidate
                });
            }
        };

        // Handle connection state changes
        pc.onconnectionstatechange = () => {
            if (pc.connectionState === 'connected') {
                addConsoleMessage(`[LAN] Connected to ${peer.name}`, 'success');
            } else if (pc.connectionState === 'disconnected' || pc.connectionState === 'failed') {
                addConsoleMessage(`[LAN] Disconnected from ${peer.name}`, 'warning');
            }
            updateLANPeersList();
        };

        // Create data channel
        const channel = pc.createDataChannel('lnk22-mesh', {
            ordered: true
        });

        channel.onopen = () => {
            addConsoleMessage(`[LAN] Data channel open to ${peer.name}`, 'success');
            peer.channel = channel;
        };

        channel.onmessage = (event) => {
            handleLANMessage(peerId, event.data);
        };

        channel.onclose = () => {
            peer.channel = null;
        };

        // Handle incoming data channels (for the answerer)
        pc.ondatachannel = (event) => {
            const ch = event.channel;
            ch.onopen = () => {
                peer.channel = ch;
                addConsoleMessage(`[LAN] Data channel received from ${peer.name}`, 'success');
            };
            ch.onmessage = (event) => {
                handleLANMessage(peerId, event.data);
            };
        };

        // Create and send offer
        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);

        sendSignalingMessage({
            type: 'offer',
            to: peerId,
            sdp: pc.localDescription
        });

    } catch (error) {
        console.error('[LAN] Failed to connect to peer:', error);
        addConsoleMessage(`[LAN] Connection failed: ${error.message}`, 'error');
    }
}

// Handle incoming RTC offer
async function handleRTCOffer(peerId, sdp) {
    let peer = lanState.peers.get(peerId);
    if (!peer) {
        peer = {
            connection: null,
            channel: null,
            name: peerId,
            lastSeen: Date.now()
        };
        lanState.peers.set(peerId, peer);
    }

    try {
        const pc = new RTCPeerConnection(rtcConfig);
        peer.connection = pc;

        pc.onicecandidate = (event) => {
            if (event.candidate) {
                sendSignalingMessage({
                    type: 'ice-candidate',
                    to: peerId,
                    candidate: event.candidate
                });
            }
        };

        pc.onconnectionstatechange = () => {
            updateLANPeersList();
        };

        pc.ondatachannel = (event) => {
            const ch = event.channel;
            ch.onopen = () => {
                peer.channel = ch;
                addConsoleMessage(`[LAN] Data channel received from ${peer.name}`, 'success');
            };
            ch.onmessage = (event) => {
                handleLANMessage(peerId, event.data);
            };
        };

        await pc.setRemoteDescription(new RTCSessionDescription(sdp));

        const answer = await pc.createAnswer();
        await pc.setLocalDescription(answer);

        sendSignalingMessage({
            type: 'answer',
            to: peerId,
            sdp: pc.localDescription
        });

    } catch (error) {
        console.error('[LAN] Failed to handle offer:', error);
    }
}

// Handle incoming RTC answer
async function handleRTCAnswer(peerId, sdp) {
    const peer = lanState.peers.get(peerId);
    if (!peer || !peer.connection) return;

    try {
        await peer.connection.setRemoteDescription(new RTCSessionDescription(sdp));
    } catch (error) {
        console.error('[LAN] Failed to handle answer:', error);
    }
}

// Handle incoming ICE candidate
async function handleICECandidate(peerId, candidate) {
    const peer = lanState.peers.get(peerId);
    if (!peer || !peer.connection) return;

    try {
        await peer.connection.addIceCandidate(new RTCIceCandidate(candidate));
    } catch (error) {
        console.error('[LAN] Failed to add ICE candidate:', error);
    }
}

// Send signaling message
function sendSignalingMessage(message) {
    if (!lanState.discoverySocket || lanState.discoverySocket.readyState !== WebSocket.OPEN) {
        return false;
    }

    message.from = lanState.localId;
    lanState.discoverySocket.send(JSON.stringify(message));
    return true;
}

// Handle incoming LAN mesh message
function handleLANMessage(peerId, data) {
    try {
        const message = JSON.parse(data);
        const peer = lanState.peers.get(peerId);
        const peerName = peer?.name || peerId;

        switch (message.type) {
            case 'chat':
                // Display as received message
                addConsoleMessage(`[LAN] ${peerName}: ${message.content}`, 'output');

                // Add to messages list
                const msgObj = {
                    id: Date.now(),
                    from: peerId,
                    fromName: peerName,
                    content: message.content,
                    timestamp: new Date(),
                    direction: 'incoming',
                    type: 'LAN'
                };
                addReceivedMessage(msgObj);
                break;

            case 'relay':
                // Relay message from radio - forward to mesh
                addConsoleMessage(`[LAN] Relay from ${peerName}: ${message.content}`, 'network');
                // Process as if it came from radio
                processSerialLine(message.content);
                break;

            case 'status':
                // Peer status update
                if (peer) {
                    peer.name = message.name || peer.name;
                }
                updateLANPeersList();
                break;
        }
    } catch (error) {
        console.error('[LAN] Failed to handle message:', error);
    }
}

// Send message to LAN peer
function sendLANMessage(peerId, content, type = 'chat') {
    const peer = lanState.peers.get(peerId);
    if (!peer || !peer.channel || peer.channel.readyState !== 'open') {
        showToast(`Cannot reach ${peer?.name || peerId}`, 'error');
        return false;
    }

    try {
        peer.channel.send(JSON.stringify({
            type: type,
            content: content,
            timestamp: Date.now()
        }));
        return true;
    } catch (error) {
        console.error('[LAN] Send failed:', error);
        return false;
    }
}

// Broadcast message to all LAN peers
function broadcastLANMessage(content, type = 'chat') {
    let sent = 0;
    for (const [peerId, peer] of lanState.peers) {
        if (sendLANMessage(peerId, content, type)) {
            sent++;
        }
    }
    return sent;
}

// Update LAN status UI
function updateLANStatus() {
    const lanStatusEl = document.getElementById('lanStatus');
    const lanPeerCount = document.getElementById('lanPeerCount');

    if (lanStatusEl) {
        lanStatusEl.className = lanState.isDiscovering ? 'lan-status connected' : 'lan-status disconnected';
        lanStatusEl.textContent = lanState.isDiscovering ? 'LAN Active' : 'LAN Inactive';
    }

    if (lanPeerCount) {
        lanPeerCount.textContent = lanState.peers.size;
    }
}

// Update LAN peers list UI
function updateLANPeersList() {
    const peersList = document.getElementById('lanPeersList');
    if (!peersList) return;

    if (lanState.peers.size === 0) {
        peersList.innerHTML = '<p class="text-muted">No LAN peers discovered</p>';
        return;
    }

    let html = '<div class="lan-peers">';
    for (const [id, peer] of lanState.peers) {
        const connected = peer.connection?.connectionState === 'connected';
        const statusClass = connected ? 'connected' : 'connecting';
        const statusIcon = connected ? '🟢' : '🟡';

        html += `
            <div class="lan-peer-item ${statusClass}">
                <span class="peer-status">${statusIcon}</span>
                <span class="peer-name">${escapeHtml(peer.name)}</span>
                <span class="peer-type">LAN</span>
                ${connected ? `<button class="btn btn-small btn-primary" onclick="sendMessageToLANPeer('${id}')">Message</button>` : '<span class="connecting-text">Connecting...</span>'}
            </div>
        `;
    }
    html += '</div>';
    peersList.innerHTML = html;

    updateLANStatus();
}

// Send message to a specific LAN peer (from UI)
function sendMessageToLANPeer(peerId) {
    const peer = lanState.peers.get(peerId);
    const name = peer?.name || peerId;

    // Switch to messages tab and set destination
    const destInput = document.getElementById('destAddress');
    if (destInput) {
        destInput.value = `lan:${name}`;
        switchTab('messagesTab');
        document.getElementById('messageText')?.focus();
    }
}

// =============================================================================
// WAN Bridge - Cross-Site Mesh Connectivity
// =============================================================================

// WAN Bridge state
const wanState = {
    socket: null,
    siteId: null,
    siteName: null,
    connected: false,
    remoteSites: new Map(),  // site_id -> { name, nodes }
    reconnectAttempts: 0,
    maxReconnectAttempts: 5,
    reconnectDelay: 5000,
    heartbeatInterval: null
};

// Generate site ID (persistent across sessions)
function getOrCreateSiteId() {
    let siteId = localStorage.getItem('lnk22_site_id');
    if (!siteId) {
        siteId = 'site-' + crypto.randomUUID();
        localStorage.setItem('lnk22_site_id', siteId);
    }
    return siteId;
}

// Connect to WAN bridge server
async function connectWANBridge(serverUrl = null) {
    if (wanState.connected) {
        addConsoleMessage('[WAN] Already connected', 'warning');
        return;
    }

    // Get or create site ID
    wanState.siteId = getOrCreateSiteId();
    wanState.siteName = state.nodeName || ('Site-' + wanState.siteId.slice(-8).toUpperCase());

    // Default to localhost if no URL provided
    const bridgeUrl = serverUrl || document.getElementById('wanBridgeUrl')?.value || 'ws://localhost:9000';

    try {
        addConsoleMessage(`[WAN] Connecting to bridge: ${bridgeUrl}`, 'info');

        wanState.socket = new WebSocket(bridgeUrl);

        wanState.socket.onopen = () => {
            // Send registration
            wanState.socket.send(JSON.stringify({
                type: 'register',
                site_id: wanState.siteId,
                name: wanState.siteName
            }));
        };

        wanState.socket.onmessage = (event) => {
            handleWANMessage(event.data);
        };

        wanState.socket.onclose = () => {
            addConsoleMessage('[WAN] Disconnected from bridge', 'warning');
            wanState.connected = false;
            clearInterval(wanState.heartbeatInterval);
            updateWANStatus();

            // Auto-reconnect
            if (wanState.reconnectAttempts < wanState.maxReconnectAttempts) {
                wanState.reconnectAttempts++;
                addConsoleMessage(`[WAN] Reconnecting in ${wanState.reconnectDelay/1000}s (attempt ${wanState.reconnectAttempts})`, 'info');
                setTimeout(() => connectWANBridge(bridgeUrl), wanState.reconnectDelay);
            }
        };

        wanState.socket.onerror = (error) => {
            addConsoleMessage('[WAN] Connection error', 'error');
            console.error('[WAN] WebSocket error:', error);
        };

    } catch (error) {
        addConsoleMessage(`[WAN] Failed to connect: ${error.message}`, 'error');
    }
}

// Disconnect from WAN bridge
function disconnectWANBridge() {
    wanState.reconnectAttempts = wanState.maxReconnectAttempts;  // Prevent auto-reconnect

    if (wanState.socket) {
        wanState.socket.close();
        wanState.socket = null;
    }

    if (wanState.heartbeatInterval) {
        clearInterval(wanState.heartbeatInterval);
        wanState.heartbeatInterval = null;
    }

    wanState.connected = false;
    wanState.remoteSites.clear();
    wanState.reconnectAttempts = 0;

    addConsoleMessage('[WAN] Disconnected', 'info');
    updateWANStatus();
    updateWANSitesList();
}

// Handle incoming WAN message
function handleWANMessage(data) {
    try {
        const message = JSON.parse(data);

        switch (message.type) {
            case 'registered':
                wanState.connected = true;
                wanState.reconnectAttempts = 0;
                addConsoleMessage(`[WAN] Registered as ${wanState.siteName}`, 'success');
                showToast('Connected to WAN Bridge', 'success');

                // Start heartbeat
                wanState.heartbeatInterval = setInterval(sendWANHeartbeat, 30000);

                // Report our nodes
                reportLocalNodes();

                updateWANStatus();
                break;

            case 'sites_list':
            case 'sites_update':
                // Update remote sites
                wanState.remoteSites.clear();
                for (const site of message.sites || []) {
                    if (site.site_id !== wanState.siteId?.slice(0, 16)) {
                        wanState.remoteSites.set(site.site_id, {
                            name: site.name,
                            nodes: site.nodes || 0
                        });
                    }
                }
                updateWANSitesList();
                break;

            case 'site_joined':
                addConsoleMessage(`[WAN] Site joined: ${message.name}`, 'success');
                wanState.remoteSites.set(message.site_id, {
                    name: message.name,
                    nodes: 0
                });
                updateWANSitesList();
                break;

            case 'site_left':
                addConsoleMessage(`[WAN] Site left: ${message.name}`, 'info');
                wanState.remoteSites.delete(message.site_id);
                updateWANSitesList();
                break;

            case 'mesh_message':
                // Incoming mesh message from another site
                addConsoleMessage(`[WAN] Message from ${message.from_site}: ${message.content}`, 'output');

                // Add to local messages
                const msgObj = {
                    id: Date.now(),
                    from: message.from,
                    fromName: message.from_name || message.from_site,
                    content: message.content,
                    timestamp: new Date(),
                    direction: 'incoming',
                    type: 'WAN'
                };
                addReceivedMessage(msgObj);

                // Relay to local radio if connected
                if (state.connected && message.relay_local) {
                    sendCommand(`send ${message.dest || 'broadcast'} [WAN:${message.from_site}] ${message.content}`);
                }
                break;

            case 'error':
                addConsoleMessage(`[WAN] Error: ${message.message}`, 'error');
                break;
        }
    } catch (error) {
        console.error('[WAN] Failed to parse message:', error);
    }
}

// Send heartbeat to keep connection alive
function sendWANHeartbeat() {
    if (wanState.socket && wanState.socket.readyState === WebSocket.OPEN) {
        wanState.socket.send(JSON.stringify({
            type: 'heartbeat',
            timestamp: Date.now()
        }));
    }
}

// Report local nodes to bridge
function reportLocalNodes() {
    if (!wanState.socket || wanState.socket.readyState !== WebSocket.OPEN) return;

    const nodes = [];
    if (state.nodeAddress) nodes.push(state.nodeAddress);
    for (const [addr, _] of state.neighbors) {
        nodes.push(addr);
    }

    wanState.socket.send(JSON.stringify({
        type: 'nodes_update',
        nodes: nodes
    }));
}

// Send message via WAN bridge
function sendWANMessage(dest, content, relayLocal = false) {
    if (!wanState.socket || wanState.socket.readyState !== WebSocket.OPEN) {
        showToast('WAN bridge not connected', 'error');
        return false;
    }

    wanState.socket.send(JSON.stringify({
        type: 'mesh_message',
        from: state.nodeAddress,
        from_name: state.nodeName,
        from_site: wanState.siteName,
        dest: dest,
        content: content,
        relay_local: relayLocal,
        timestamp: Date.now()
    }));

    addConsoleMessage(`[WAN] Sent to ${dest || 'all sites'}: ${content}`, 'command');
    return true;
}

// Update WAN status UI
function updateWANStatus() {
    const wanStatusEl = document.getElementById('wanStatus');
    const wanSiteCount = document.getElementById('wanSiteCount');
    const wanConnectBtn = document.getElementById('wanConnectBtn');

    if (wanStatusEl) {
        wanStatusEl.className = wanState.connected ? 'wan-status connected' : 'wan-status disconnected';
        wanStatusEl.textContent = wanState.connected ? 'WAN Connected' : 'WAN Disconnected';
    }

    if (wanSiteCount) {
        wanSiteCount.textContent = wanState.remoteSites.size;
    }

    if (wanConnectBtn) {
        wanConnectBtn.textContent = wanState.connected ? 'Disconnect WAN' : 'Connect WAN';
        wanConnectBtn.className = wanState.connected ? 'btn btn-danger' : 'btn btn-primary';
    }
}

// Update WAN sites list UI
function updateWANSitesList() {
    const sitesList = document.getElementById('wanSitesList');
    if (!sitesList) return;

    if (wanState.remoteSites.size === 0) {
        sitesList.innerHTML = '<p class="text-muted">No remote sites connected</p>';
        return;
    }

    let html = '<div class="wan-sites">';
    for (const [id, site] of wanState.remoteSites) {
        html += `
            <div class="wan-site-item">
                <span class="site-icon">🌐</span>
                <div class="site-info">
                    <span class="site-name">${escapeHtml(site.name)}</span>
                    <span class="site-nodes">${site.nodes} nodes</span>
                </div>
                <button class="btn btn-small btn-primary" onclick="sendMessageToWANSite('${escapeHtml(site.name)}')">Message</button>
            </div>
        `;
    }
    html += '</div>';
    sitesList.innerHTML = html;

    updateWANStatus();
}

// Send message to a specific WAN site
function sendMessageToWANSite(siteName) {
    const destInput = document.getElementById('destAddress');
    if (destInput) {
        destInput.value = `wan:${siteName}`;
        switchTab('messagesTab');
        document.getElementById('messageText')?.focus();
    }
}

// =============================================================================
// Serial Data Parser - Complete Implementation
// =============================================================================

function processSerialLine(line) {
    addConsoleMessage(line, 'output');

    // PRIORITY: Handle message capture first (before section handling)
    // This ensures multi-line messages are properly captured
    if (pendingMessage) {
        // Timeout safety: if message capture takes too long, reset state
        const captureAge = Date.now() - pendingMessage.timestamp.getTime();
        if (captureAge > 5000) {  // 5 second timeout
            console.log('[MSG] Message capture timeout, resetting');
            pendingMessage = null;
            messageContentStarted = false;
        }
    }

    if (pendingMessage) {
        // Start capturing after the separator line (----)
        if (line.match(/^-{3,}$/)) {
            messageContentStarted = true;
            return;
        }

        // End of message (==== must be at least 8 equals to match firmware format)
        if (line.match(/^={8,}$/)) {
            const trimmedContent = pendingMessage.content.trim();
            if (trimmedContent) {
                // Dedupe: check if we recently added same content from same source
                const isDupe = state.messages.some(m =>
                    m.content === trimmedContent &&
                    m.from === pendingMessage.from &&
                    Date.now() - new Date(m.timestamp).getTime() < 10000
                );
                if (!isDupe) {
                    // Trim and clean content before adding
                    pendingMessage.content = trimmedContent;
                    addReceivedMessage(pendingMessage);
                    console.log(`[MSG] Message captured from ${pendingMessage.fromName}: "${trimmedContent}"`);
                } else {
                    console.log(`[MSG] Skipping duplicate message from ${pendingMessage.fromName}`);
                }
            } else {
                console.log(`[MSG] Empty message discarded from ${pendingMessage.fromName}`);
            }
            pendingMessage = null;
            messageContentStarted = false;
            return;
        }

        // Accumulate content only after we've seen the separator
        if (messageContentStarted) {
            if (pendingMessage.content) {
                pendingMessage.content += '\n' + line;
            } else {
                pendingMessage.content = line;
            }
            return;
        }

        // Skip other lines while waiting for separator (don't accumulate junk)
        return;
    }

    // Check for message start - must be exactly "MESSAGE from 0xXXXX (TYPE)"
    let msgMatch = line.match(/^MESSAGE from 0x([0-9A-Fa-f]+)\s*\((\w+)\)$/);
    if (msgMatch) {
        const fromAddr = '0x' + msgMatch[1].toUpperCase();
        const msgType = msgMatch[2];  // BROADCAST or DIRECT
        const friendlyName = getDisplayName(fromAddr);  // Always use friendly name
        pendingMessage = {
            from: fromAddr,
            fromName: friendlyName,
            type: msgType,
            content: '',
            timestamp: new Date()
        };
        messageContentStarted = false;
        console.log(`[MSG] Starting message capture from ${friendlyName}`);
        return;
    }

    // Track section headers for multi-line parsing
    // Note: Section headers are like "=== Status ===" with text inside
    // Message boundaries are just equals signs (40 chars) with no text
    if (line.startsWith('===') && line.includes(' ')) {
        if (line.includes('LNK-22 Status')) state.currentSection = 'status';
        else if (line.includes('Link Status') || line.includes('Link ===')) state.currentSection = 'link';
        else if (line.includes('Group')) state.currentSection = 'group';
        else if (line.includes('Store-and-Forward')) state.currentSection = 'sf';
        else if (line.includes('Adaptive Data Rate')) state.currentSection = 'adr';
        else if (line.includes('Neighbor')) state.currentSection = 'neighbors';
        else if (line.includes('Route') || line.includes('Routing')) state.currentSection = 'routes';
        else if (line.includes('Emergency') || line.includes('SOS')) state.currentSection = 'sos';
        else if (line.includes('MAC') || line.includes('TDMA') || line.includes('Hybrid')) state.currentSection = 'mac';
        else if (line.includes('Radio')) state.currentSection = 'radio';
        else state.currentSection = null;
        return;
    }

    // End of section - short equals lines (up to 25 chars) are section terminators
    // Longer ones (40 chars) are message boundaries
    if (line.match(/^={3,25}$/)) {
        state.currentSection = null;
        updateAllDisplays();
        return;
    }

    // Parse based on current section or line prefix
    if (line.startsWith('[GPS]')) {
        parseGPSLine(line);
    } else if (line.startsWith('[RADIO]')) {
        parseRadioLine(line);
    } else if (line.startsWith('[MESH]')) {
        parseMeshLine(line);
    } else if (line.startsWith('[LINK]')) {
        parseLinkLogLine(line);
    } else if (line.startsWith('[GROUP]')) {
        parseGroupLogLine(line);
    } else if (line.startsWith('[SF]') || line.startsWith('[STORE]')) {
        parseSFLogLine(line);
    } else if (line.startsWith('[ADR]')) {
        parseADRLogLine(line);
    } else if (line.startsWith('[SOS]') || line.startsWith('[EMERGENCY]')) {
        parseEmergencyLogLine(line);
    } else if (line.startsWith('[DTN]')) {
        parseDTNLogLine(line);
    } else if (line.includes('SOS ACTIVATED')) {
        state.emergency.active = true;
        showToast('🚨 EMERGENCY SOS ACTIVATED!', 'error');
        updateEmergencyDisplay();
    } else if (line.includes('Emergency cancelled') || line.includes('SOS cancelled')) {
        state.emergency.active = false;
        showToast('Emergency SOS cancelled', 'info');
        updateEmergencyDisplay();
    } else {
        // Section-specific parsing
        switch (state.currentSection) {
            case 'status':
                parseStatusLine(line);
                break;
            case 'link':
                parseLinkStatusLine(line);
                break;
            case 'group':
                parseGroupStatusLine(line);
                break;
            case 'sf':
                parseSFStatusLine(line);
                break;
            case 'adr':
                parseADRStatusLine(line);
                break;
            case 'neighbors':
                parseNeighborLine(line);
                break;
            case 'routes':
                parseRouteLine(line);
                break;
            case 'mac':
                parseMACStatusLine(line);
                break;
            case 'radio':
                parseRadioStatusLine(line);
                break;
            default:
                parseGenericLine(line);
        }
    }

    // Parse firmware version
    if (line.includes('LNK-22') && line.match(/v?\d+\.\d+\.\d+/)) {
        const match = line.match(/v?(\d+\.\d+\.\d+)/);
        if (match) parseFirmwareVersion(match[1]);
    }
}

// =============================================================================
// Status Parsing
// =============================================================================

function parseStatusLine(line) {
    // Node: Node-0100 (0x1FA60100) or Node: Alpha (0xC79E6B8D)
    let match = line.match(/Node:\s*(.+?)\s*\(0x([0-9A-Fa-f]+)\)/);
    if (match) {
        state.nodeName = match[1];
        state.nodeAddress = '0x' + match[2].toUpperCase();
        state.nodeNames.set(state.nodeAddress, state.nodeName);
        console.log(`Parsed node: ${state.nodeName} ${state.nodeAddress}`);
        updateDashboard();
        return;
    }

    // Uptime: 1180 seconds
    match = line.match(/Uptime:\s*(\d+)/);
    if (match) {
        state.stats.uptime = parseInt(match[1]);
        updateDashboard();
        return;
    }

    // Radio RSSI: -63 dBm
    match = line.match(/Radio RSSI:\s*(-?\d+)/);
    if (match) {
        state.stats.rssi = parseInt(match[1]);
        updateDashboard();
        return;
    }

    // Radio SNR: 9 dB
    match = line.match(/Radio SNR:\s*(-?\d+)/);
    if (match) {
        state.stats.snr = parseInt(match[1]);
        updateDashboard();
        return;
    }

    // Packets Sent: 40
    match = line.match(/Packets Sent:\s*(\d+)/);
    if (match) {
        state.stats.packetsSent = parseInt(match[1]);
        updateDashboard();
        return;
    }

    // Packets Received: 95
    match = line.match(/Packets Received:\s*(\d+)/);
    if (match) {
        state.stats.packetsReceived = parseInt(match[1]);
        updateDashboard();
        return;
    }

    // Channel: 0
    match = line.match(/Channel:\s*(\d+)/);
    if (match) {
        state.stats.channel = parseInt(match[1]);
        updateDashboard();
        return;
    }

    // Neighbors: 2
    match = line.match(/Neighbors:\s*(\d+)/);
    if (match) {
        // Just count, actual neighbors come from neighbors command
        return;
    }
}

// =============================================================================
// Link Parsing
// =============================================================================

function parseLinkStatusLine(line) {
    // Active links: 0/8
    let match = line.match(/Active links:\s*(\d+)\/(\d+)/);
    if (match) {
        state.links.active = parseInt(match[1]);
        state.links.max = parseInt(match[2]);
        return;
    }

    // Link entry: 0x12345678 ACTIVE (FS, initiator)
    match = line.match(/0x([0-9A-Fa-f]+)\s+(\w+)\s*\(([^)]+)\)/);
    if (match) {
        const addr = '0x' + match[1].toUpperCase();
        const linkState = match[2];
        const flags = match[3];
        state.links.list.set(addr, {
            address: addr,
            state: linkState,
            forwardSecrecy: flags.includes('FS'),
            initiator: flags.includes('initiator')
        });
    }
}

function parseLinkLogLine(line) {
    if (line.includes('Link established')) {
        const match = line.match(/to\s+0x([0-9A-Fa-f]+)/);
        if (match) {
            const addr = '0x' + match[1].toUpperCase();
            state.links.list.set(addr, {
                address: addr,
                state: 'ACTIVE',
                forwardSecrecy: false,
                initiator: true,
                established: Date.now()
            });
            state.links.active = state.links.list.size;
            showToast(`Secure link established with ${getDisplayName(addr)}`, 'success');
            updateLinksDisplay();
        }
    } else if (line.includes('Forward secrecy enabled')) {
        // Mark most recent link as having FS
        for (const [addr, link] of state.links.list) {
            if (!link.forwardSecrecy) {
                link.forwardSecrecy = true;
                break;
            }
        }
        updateLinksDisplay();
    } else if (line.includes('Closed link') || line.includes('Link timeout')) {
        const match = line.match(/0x([0-9A-Fa-f]+)/);
        if (match) {
            const addr = '0x' + match[1].toUpperCase();
            state.links.list.delete(addr);
            state.links.active = state.links.list.size;
            updateLinksDisplay();
        }
    } else if (line.includes('Link request from')) {
        const match = line.match(/from\s+0x([0-9A-Fa-f]+)/);
        if (match) {
            showToast(`Link request from ${match[1]}`, 'info');
        }
    }
}

// =============================================================================
// Group Parsing
// =============================================================================

function parseGroupStatusLine(line) {
    // Groups: 0/8
    let match = line.match(/Groups:\s*(\d+)\/(\d+)/);
    if (match) {
        state.groups.count = parseInt(match[1]);
        state.groups.max = parseInt(match[2]);
        return;
    }

    // Sent: 0
    match = line.match(/Sent:\s*(\d+)/);
    if (match) {
        state.groups.stats.sent = parseInt(match[1]);
        return;
    }

    // Received: 0
    match = line.match(/Received:\s*(\d+)/);
    if (match) {
        state.groups.stats.received = parseInt(match[1]);
        return;
    }

    // Decrypted: 0
    match = line.match(/Decrypted:\s*(\d+)/);
    if (match) {
        state.groups.stats.decrypted = parseInt(match[1]);
        return;
    }

    // Group entry: GroupName (ID: 12345, 3 members)
    match = line.match(/^\s*(\w+)\s+\(ID:\s*(\d+)/);
    if (match) {
        state.groups.list.set(match[1], {
            name: match[1],
            id: match[2]
        });
    }
}

function parseGroupLogLine(line) {
    if (line.includes('Created group:') || line.includes('Joined group:')) {
        const match = line.match(/(?:Created|Joined) group:\s*(\w+)/);
        if (match) {
            state.groups.list.set(match[1], { name: match[1], joined: Date.now() });
            state.groups.count = state.groups.list.size;
            showToast(`Group "${match[1]}" active`, 'success');
            updateGroupsDisplay();
        }
    } else if (line.includes('Left group:')) {
        const match = line.match(/Left group:\s*(\w+)/);
        if (match) {
            state.groups.list.delete(match[1]);
            state.groups.count = state.groups.list.size;
            updateGroupsDisplay();
        }
    } else if (line.includes('Group message from')) {
        showToast('New group message received', 'info');
    }
}

// =============================================================================
// Store-and-Forward Parsing
// =============================================================================

function parseSFStatusLine(line) {
    // Queued: 0/32
    let match = line.match(/Queued:\s*(\d+)\/(\d+)/);
    if (match) {
        state.storeForward.queued = parseInt(match[1]);
        state.storeForward.max = parseInt(match[2]);
        return;
    }

    // Delivered: 0
    match = line.match(/Delivered:\s*(\d+)/);
    if (match) {
        state.storeForward.delivered = parseInt(match[1]);
        return;
    }

    // Expired: 0
    match = line.match(/Expired:\s*(\d+)/);
    if (match) {
        state.storeForward.expired = parseInt(match[1]);
        return;
    }
}

function parseSFLogLine(line) {
    if (line.includes('Stored message')) {
        state.storeForward.queued++;
        showToast('Message queued for offline node', 'info');
        updateSFDisplay();
    } else if (line.includes('Delivered stored')) {
        state.storeForward.queued = Math.max(0, state.storeForward.queued - 1);
        state.storeForward.delivered++;
        showToast('Stored message delivered!', 'success');
        updateSFDisplay();
    }
}

// =============================================================================
// ADR Parsing
// =============================================================================

function parseADRStatusLine(line) {
    // ADR: ENABLED
    let match = line.match(/ADR:\s*(\w+)/);
    if (match) {
        state.adr.enabled = (match[1] === 'ENABLED');
        console.log(`ADR enabled: ${state.adr.enabled}`);
        updateADRDisplay();
        return;
    }

    // Default SF: 10
    match = line.match(/Default SF:\s*(\d+)/);
    if (match) {
        state.adr.defaultSF = parseInt(match[1]);
        updateADRDisplay();
        return;
    }

    // SF changes: 0
    match = line.match(/SF changes:\s*(\d+)/);
    if (match) {
        state.adr.sfChanges = parseInt(match[1]);
        updateADRDisplay();
        return;
    }

    // SF7:  0 (with possible extra spaces)
    match = line.match(/SF(\d+):\s*(\d+)/);
    if (match) {
        state.adr.packetsBySF[parseInt(match[1])] = parseInt(match[2]);
        updateADRDisplay();
        return;
    }
}

function parseADRLogLine(line) {
    if (line.includes('SF changed')) {
        state.adr.sfChanges++;
        const match = line.match(/to SF(\d+)/);
        if (match) {
            showToast(`ADR: Switched to SF${match[1]}`, 'info');
        }
        updateADRDisplay();
    }
}

// =============================================================================
// Emergency Parsing
// =============================================================================

function parseEmergencyLogLine(line) {
    if (line.includes('activated') || line.includes('ACTIVATED')) {
        state.emergency.active = true;
        const match = line.match(/Type:\s*(.+)/);
        if (match) state.emergency.type = match[1];
        updateEmergencyDisplay();
    } else if (line.includes('cancelled') || line.includes('deactivated')) {
        state.emergency.active = false;
        state.emergency.type = null;
        updateEmergencyDisplay();
    } else if (line.includes('SOS received from')) {
        const match = line.match(/from\s+0x([0-9A-Fa-f]+)/);
        if (match) {
            showToast(`🚨 SOS received from ${match[1]}!`, 'error');
        }
    }
}

// =============================================================================
// DTN Parsing
// =============================================================================

function parseDTNLogLine(line) {
    if (line.includes('Bundle created')) {
        state.dtn.stats.created++;
        state.dtn.stats.pending++;
        const match = line.match(/ID[:\s]*(\d+)/);
        if (match) {
            state.dtn.bundles.push({ id: match[1], status: 'pending', created: Date.now() });
        }
        updateDTNDisplay();
    } else if (line.includes('Bundle delivered')) {
        state.dtn.stats.delivered++;
        state.dtn.stats.pending = Math.max(0, state.dtn.stats.pending - 1);
        showToast('DTN bundle delivered', 'success');
        updateDTNDisplay();
    }
}

// =============================================================================
// GPS Parsing
// =============================================================================

function parseGPSLine(line) {
    // [GPS] No fix (0 sats)
    let match = line.match(/No fix\s*\((\d+)\s*sats?\)/);
    if (match) {
        state.gps.satellites = parseInt(match[1]);
        state.gps.fix = false;
        return;
    }

    // [GPS] Fix: Lat 40.123456, Lon -74.123456, Alt 100m (8 sats)
    match = line.match(/Lat\s*(-?[\d.]+).*Lon\s*(-?[\d.]+).*Alt\s*([\d.]+).*\((\d+)\s*sats?\)/);
    if (match) {
        state.gps.latitude = parseFloat(match[1]);
        state.gps.longitude = parseFloat(match[2]);
        state.gps.altitude = parseFloat(match[3]);
        state.gps.satellites = parseInt(match[4]);
        state.gps.fix = true;
        updateGPSMap();
    }
}

// =============================================================================
// Radio Parsing
// =============================================================================

function parseRadioLine(line) {
    // [RADIO] RX: 59 bytes, RSSI=-58 dBm, SNR=8 dB
    const rssiMatch = line.match(/RSSI[=:\s]*(-?\d+)/i);
    const snrMatch = line.match(/SNR[=:\s]*(-?\d+)/i);

    if (rssiMatch) state.stats.rssi = parseInt(rssiMatch[1]);
    if (snrMatch) state.stats.snr = parseInt(snrMatch[1]);

    if (line.includes('RX:')) {
        state.stats.packetsReceived++;
    } else if (line.includes('TX:') || line.includes('TX complete')) {
        state.stats.packetsSent++;
    }
}

// =============================================================================
// Mesh Parsing
// =============================================================================

function parseMeshLine(line) {
    if (line.includes('Beacon from:')) {
        const match = line.match(/from:\s*(.+)/);
        if (match) {
            // Could track beacon sources
        }
    } else if (line.includes('New neighbor')) {
        const match = line.match(/0x([0-9A-Fa-f]+)/);
        if (match) {
            showToast(`New neighbor: ${match[1]}`, 'success');
        }
    }
}

// =============================================================================
// Neighbor Parsing
// =============================================================================

function parseNeighborLine(line) {
    // Skip headers
    if (line.includes('===') || line.includes('Address') || line.includes('---') || line.includes('(none)') || line.includes('no neighbor')) {
        return;
    }

    // Multi-path format: "NodeName [BLE,LoRa] *BLE* LoRa:-64/9 BLE:-45 (5 pkts, 10s ago)"
    // Single-path format: "NodeName [LoRa] *LoRa* LoRa:-64/9 (5 pkts, 10s ago)"
    // Old format: "NodeName [LoRa] RSSI:-64 SNR:9 (5 pkts, 10s ago)"

    // New multi-path format with signal info per interface
    // Pattern: Name [interfaces] *preferred* Interface:rssi/snr ... (pkts, age)
    let match = line.match(/^\s*([\w-]+|0x[0-9A-Fa-f]+)\s+\[([^\]]+)\]\s+\*(\w+)\*(.+)\((\d+)\s+pkts?,\s*(\d+)s/i);
    if (match) {
        let nameOrAddr = match[1];
        let interfaces = match[2].split(',').map(s => s.trim().toUpperCase());
        let preferred = match[3].toUpperCase();
        let signalSection = match[4];
        let packets = parseInt(match[5]);
        let age = parseInt(match[6]);

        let addr = nameOrAddr;
        let name = null;

        // Check if it's a hex address
        if (nameOrAddr.toLowerCase().startsWith('0x')) {
            addr = '0x' + nameOrAddr.slice(2).toUpperCase();
            name = state.nodeNames.get(addr) || null;
        } else {
            name = nameOrAddr;
            let foundAddr = null;
            for (const [a, n] of state.nodeNames) {
                if (n === name) { foundAddr = a; break; }
            }
            addr = foundAddr || name;
        }

        // Parse signal info per interface (e.g., "LoRa:-64/9 BLE:-45")
        let signalInfo = {};
        let rssi = -100, snr = 0;

        // Extract LoRa signal: "LoRa:-64/9"
        let loraMatch = signalSection.match(/LoRa:(-?\d+)\/(-?\d+)/i);
        if (loraMatch) {
            signalInfo.LORA = { rssi: parseInt(loraMatch[1]), snr: parseInt(loraMatch[2]) };
        }

        // Extract BLE signal: "BLE:-45"
        let bleMatch = signalSection.match(/BLE:(-?\d+)/i);
        if (bleMatch) {
            signalInfo.BLE = { rssi: parseInt(bleMatch[1]), snr: 0 };
        }

        // Extract LAN: "LAN:ok"
        if (signalSection.includes('LAN:ok')) {
            signalInfo.LAN = { rssi: 0, snr: 0 };
        }

        // Get primary signal from preferred interface
        if (signalInfo[preferred]) {
            rssi = signalInfo[preferred].rssi;
            snr = signalInfo[preferred].snr;
        } else if (Object.keys(signalInfo).length > 0) {
            let first = Object.values(signalInfo)[0];
            rssi = first.rssi;
            snr = first.snr;
        }

        state.neighbors.set(addr, {
            address: addr,
            name: name,
            interfaces: interfaces,           // Array of available interfaces
            preferred: preferred,             // Best interface
            signalInfo: signalInfo,           // Per-interface signal info
            rssi: rssi,                       // Primary RSSI (from preferred)
            snr: snr,                         // Primary SNR (from preferred)
            packets: packets,
            age: age,
            lastSeen: Date.now()
        });
        console.log(`[ARP] Multi-path neighbor: ${name || addr} [${interfaces.join(',')}] *${preferred}*`);
        updateNeighborGrid();
        updateARPTable();
        return;
    }

    // Legacy format with single interface: Name/Address [IFACE] RSSI:X SNR:Y (Z pkts, Ws ago)
    match = line.match(/^\s*([\w-]+|0x[0-9A-Fa-f]+)\s+\[(\w+)\]\s+RSSI:(-?\d+)\s+SNR:(-?\d+)\s+\((\d+)\s+pkts?,\s*(\d+)s/i);
    if (match) {
        let nameOrAddr = match[1];
        let iface = match[2].toUpperCase();
        let addr = nameOrAddr;
        let name = null;

        if (nameOrAddr.toLowerCase().startsWith('0x')) {
            addr = '0x' + nameOrAddr.slice(2).toUpperCase();
            name = state.nodeNames.get(addr) || null;
        } else {
            name = nameOrAddr;
            let foundAddr = null;
            for (const [a, n] of state.nodeNames) {
                if (n === name) { foundAddr = a; break; }
            }
            addr = foundAddr || name;
        }

        state.neighbors.set(addr, {
            address: addr,
            name: name,
            interfaces: [iface],
            preferred: iface,
            signalInfo: { [iface]: { rssi: parseInt(match[3]), snr: parseInt(match[4]) } },
            rssi: parseInt(match[3]),
            snr: parseInt(match[4]),
            packets: parseInt(match[5]),
            age: parseInt(match[6]),
            lastSeen: Date.now()
        });
        console.log(`[ARP] Parsed neighbor: ${name || addr} [${iface}] RSSI:${match[3]}`);
        updateNeighborGrid();
        updateARPTable();
        return;
    }

    // Fallback: Old format without interface
    match = line.match(/^\s*([\w-]+|0x[0-9A-Fa-f]+)\s+RSSI:(-?\d+)\s+SNR:(-?\d+)\s+\((\d+)\s+pkts?,\s*(\d+)s/i);
    if (match) {
        let nameOrAddr = match[1];
        let addr = nameOrAddr;
        let name = null;

        if (nameOrAddr.toLowerCase().startsWith('0x')) {
            addr = '0x' + nameOrAddr.slice(2).toUpperCase();
            name = state.nodeNames.get(addr) || null;
        } else {
            name = nameOrAddr;
            let foundAddr = null;
            for (const [a, n] of state.nodeNames) {
                if (n === name) { foundAddr = a; break; }
            }
            addr = foundAddr || name;
        }

        state.neighbors.set(addr, {
            address: addr,
            name: name,
            interface: 'LORA',  // Default to LoRa for old format
            rssi: parseInt(match[2]),
            snr: parseInt(match[3]),
            packets: parseInt(match[4]),
            age: parseInt(match[5]),
            lastSeen: Date.now()
        });
        console.log(`[ARP] Parsed neighbor (old format): ${name || addr} RSSI:${match[2]}`);
        updateNeighborGrid();
        updateARPTable();
        return;
    }
}

// =============================================================================
// Route Parsing
// =============================================================================

function parseRouteLine(line) {
    if (line.includes('===') || line.includes('Destination') || line.includes('---') || line.includes('(none)')) {
        return;
    }

    // 0xDEST  0xNEXTHOP  HOPS  QUALITY  AGE
    const match = line.match(/0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+(\d+)\s+(\d+)\s+(\d+)/);
    if (match) {
        const dest = '0x' + match[1].toUpperCase();
        state.routes.set(dest, {
            destination: dest,
            nextHop: '0x' + match[2].toUpperCase(),
            hops: parseInt(match[3]),
            quality: parseInt(match[4]),
            age: parseInt(match[5])
        });
    }
}

// =============================================================================
// Generic/Name Parsing
// =============================================================================

// State for multi-line message parsing
let pendingMessage = null;
let messageContentStarted = false;

function parseGenericLine(line) {
    // Message parsing is now handled at the top of processSerialLine
    // This function handles other generic line formats

    // Node name: Alpha (0x4D77048F) or LNK-048F (0x4D77048F)
    let match = line.match(/Node(?:\s+name)?:\s*([\w-]+)\s+\(0x([0-9A-Fa-f]+)\)/);
    if (match) {
        state.nodeName = match[1];
        state.nodeAddress = '0x' + match[2].toUpperCase();
        state.nodeNames.set(state.nodeAddress, state.nodeName);
        updateDashboard();
        return;
    }

    // Name list local: * Alpha (0x4D77048F) [local] or * LNK-048F (0x...) [local]
    match = line.match(/\*\s+([\w-]+)\s+\(0x([0-9A-Fa-f]+)\)\s+\[local\]/);
    if (match) {
        state.nodeName = match[1];
        state.nodeAddress = '0x' + match[2].toUpperCase();
        state.nodeNames.set(state.nodeAddress, state.nodeName);
        updateDashboard();
        return;
    }

    // Name list remote: Alpha (0xDAD930F0) or LNK-ABC1 (0x...)
    match = line.match(/^\s+([\w-]+)\s+\(0x([0-9A-Fa-f]+)\)/);
    if (match && !line.includes('[local]')) {
        const addr = '0x' + match[2].toUpperCase();
        state.nodeNames.set(addr, match[1]);
        return;
    }

    // Added name 'X' for 0xADDRESS
    match = line.match(/Added name '([\w-]+)' for 0x([0-9A-Fa-f]+)/);
    if (match) {
        const addr = '0x' + match[2].toUpperCase();
        state.nodeNames.set(addr, match[1]);
        showToast(`Name '${match[1]}' added`, 'success');
        return;
    }
}

// =============================================================================
// Version Parsing
// =============================================================================

function parseFirmwareVersion(version) {
    if (state.firmwareVersion === version) return;

    state.firmwareVersion = version;
    console.log(`Firmware version: ${version}`);

    const fwParts = version.split('.').map(Number);
    const minParts = MIN_FIRMWARE_VERSION.split('.').map(Number);

    let firmwareTooOld = false;
    for (let i = 0; i < 3; i++) {
        if (fwParts[i] < minParts[i]) { firmwareTooOld = true; break; }
        else if (fwParts[i] > minParts[i]) break;
    }

    state.versionMismatch = (version !== WEB_CLIENT_VERSION);

    updateVersionDisplay();

    if (firmwareTooOld) {
        showToast(`Firmware ${version} outdated. Update to ${MIN_FIRMWARE_VERSION}+`, 'error');
    } else if (state.versionMismatch) {
        showToast(`Version mismatch: FW ${version}, Web ${WEB_CLIENT_VERSION}`, 'warning');
    } else {
        showToast(`Connected to LNK-22 v${version}`, 'success');
    }
}

// =============================================================================
// UI Update Functions
// =============================================================================

function updateAllDisplays() {
    updateDashboard();
    updateLinksDisplay();
    updateGroupsDisplay();
    updateSFDisplay();
    updateADRDisplay();
    updateEmergencyDisplay();
    updateNeighborGrid();
    updateRoutingTable();
    updateNetworkGraph();
    updateTDMADisplay();
    updateARPTable();
    updateRadioDisplay();
}

function updateConnectionStatus(connected) {
    const statusDot = document.getElementById('statusIndicator');
    const statusText = document.getElementById('statusText');
    const statusBadge = document.getElementById('deviceStatus');
    const connectBtn = document.getElementById('connectBtn');
    const sendCmdBtn = document.getElementById('sendCmdBtn');
    const sendBtn = document.getElementById('sendBtn');

    if (connected) {
        if (statusDot) statusDot.className = 'status-dot connected';
        if (statusText) statusText.textContent = 'Connected';
        if (statusBadge) {
            statusBadge.className = 'badge badge-success';
            statusBadge.textContent = 'Online';
        }
        if (connectBtn) connectBtn.textContent = '🔌 Disconnect';
        if (sendCmdBtn) sendCmdBtn.disabled = false;
        if (sendBtn) sendBtn.disabled = false;
    } else {
        if (statusDot) statusDot.className = 'status-dot disconnected';
        if (statusText) statusText.textContent = 'Disconnected';
        if (statusBadge) {
            statusBadge.className = 'badge badge-gray';
            statusBadge.textContent = 'Offline';
        }
        if (connectBtn) connectBtn.textContent = '🔌 Connect Device';
        if (sendCmdBtn) sendCmdBtn.disabled = true;
        if (sendBtn) sendBtn.disabled = true;
    }
}

function updateDashboard() {
    // Node info - show friendly name only, no hex
    const deviceAddr = document.getElementById('deviceAddress');
    if (deviceAddr) {
        deviceAddr.textContent = state.nodeName || getDisplayName(state.nodeAddress) || '-';
    }

    // Stats
    setElementText('deviceUptime', formatUptime(state.stats.uptime));
    setElementText('deviceRSSI', `${state.stats.rssi} dBm`);
    setElementText('deviceSNR', `${state.stats.snr} dB`);
    setElementText('neighborCount', state.neighbors.size);
    setElementText('routeCount', state.routes.size);
    setElementText('totalPackets', state.stats.packetsReceived + state.stats.packetsSent);
    setElementText('packetsReceived', state.stats.packetsReceived);
    setElementText('packetsSent', state.stats.packetsSent);
    setElementText('activeNeighbors', state.neighbors.size);

    // Feature stats
    setElementText('activeLinkCount', state.links.active);
    setElementText('activeGroupCount', state.groups.count);
    setElementText('sfQueueCount', state.storeForward.queued);

    // Last update
    const lastUpdate = document.getElementById('lastUpdate');
    if (lastUpdate) lastUpdate.textContent = `Last updated: ${new Date().toLocaleTimeString()}`;

    updateKnownNodesList();
}

function updateLinksDisplay() {
    const container = document.getElementById('linksStatus');
    if (!container) return;

    setElementText('activeLinkCount', state.links.active);

    if (state.links.list.size === 0) {
        container.innerHTML = `
            <p class="text-muted">No active secure links</p>
            <p class="text-small">Use <code>link &lt;node&gt;</code> to establish a link</p>
        `;
        return;
    }

    let html = '<div class="links-list">';
    for (const [addr, link] of state.links.list) {
        const name = getDisplayName(addr);
        const fsIcon = link.forwardSecrecy ? '🔒' : '🔓';
        const fsClass = link.forwardSecrecy ? 'badge-success' : 'badge-warning';
        const fsText = link.forwardSecrecy ? 'Forward Secrecy' : 'Standard';
        html += `
            <div class="link-item">
                <div class="link-info">
                    <span class="link-peer">${fsIcon} ${escapeHtml(name)}</span>
                </div>
                <div class="link-meta">
                    <span class="badge ${fsClass}">${fsText}</span>
                    <span class="badge badge-gray">${link.state || 'ACTIVE'}</span>
                </div>
                <button class="btn btn-small btn-danger" onclick="closeLink('${escapeHtml(name)}')">Close</button>
            </div>
        `;
    }
    html += '</div>';
    container.innerHTML = html;
}

function updateGroupsDisplay() {
    const container = document.getElementById('groupsStatus');
    if (!container) return;

    setElementText('activeGroupCount', state.groups.count);

    if (state.groups.list.size === 0) {
        container.innerHTML = `
            <p class="text-muted">No active groups</p>
            <p class="text-small">Use <code>group create &lt;name&gt;</code> to create a group</p>
        `;
        return;
    }

    let html = '<div class="groups-list">';
    for (const [name, group] of state.groups.list) {
        html += `
            <div class="group-item">
                <span class="group-name">👥 ${escapeHtml(name)}</span>
                <div class="group-actions">
                    <button class="btn btn-small btn-primary" onclick="sendGroupMessage('${name}')">Send</button>
                    <button class="btn btn-small btn-danger" onclick="leaveGroup('${name}')">Leave</button>
                </div>
            </div>
        `;
    }
    html += '</div>';

    // Stats
    html += `
        <div class="group-stats">
            <small>Sent: ${state.groups.stats.sent} | Received: ${state.groups.stats.received} | Decrypted: ${state.groups.stats.decrypted}</small>
        </div>
    `;

    container.innerHTML = html;
}

function updateSFDisplay() {
    const container = document.getElementById('sfStatus');
    if (!container) return;

    setElementText('sfQueueCount', state.storeForward.queued);

    container.innerHTML = `
        <div class="sf-stats">
            <div class="sf-stat">
                <span class="sf-value">${state.storeForward.queued}</span>
                <span class="sf-label">Queued</span>
            </div>
            <div class="sf-stat">
                <span class="sf-value">${state.storeForward.delivered}</span>
                <span class="sf-label">Delivered</span>
            </div>
            <div class="sf-stat">
                <span class="sf-value">${state.storeForward.expired}</span>
                <span class="sf-label">Expired</span>
            </div>
        </div>
        <div class="sf-capacity">
            <div class="progress-bar">
                <div class="progress-fill" style="width: ${(state.storeForward.queued / state.storeForward.max) * 100}%"></div>
            </div>
            <small>${state.storeForward.queued}/${state.storeForward.max} capacity</small>
        </div>
    `;
}

function updateADRDisplay() {
    const container = document.getElementById('adrStatus');
    if (!container) return;

    const statusClass = state.adr.enabled ? 'badge-success' : 'badge-gray';
    const statusText = state.adr.enabled ? 'ENABLED' : 'DISABLED';

    container.innerHTML = `
        <div class="adr-header">
            <span class="badge ${statusClass}">${statusText}</span>
            <span>Current SF: ${state.adr.defaultSF}</span>
            <span>Changes: ${state.adr.sfChanges}</span>
        </div>
        <div class="adr-stats">
            <small>Packets by SF:
                SF7:${state.adr.packetsBySF[7]} |
                SF8:${state.adr.packetsBySF[8]} |
                SF9:${state.adr.packetsBySF[9]} |
                SF10:${state.adr.packetsBySF[10]} |
                SF11:${state.adr.packetsBySF[11]} |
                SF12:${state.adr.packetsBySF[12]}
            </small>
        </div>
        <div class="adr-controls">
            <button class="btn btn-small ${state.adr.enabled ? 'btn-danger' : 'btn-success'}"
                    onclick="toggleADR()">
                ${state.adr.enabled ? 'Disable ADR' : 'Enable ADR'}
            </button>
            <button class="btn btn-small btn-secondary" onclick="sendCommand('adr scan')">Scan Networks</button>
        </div>
    `;
}

function updateEmergencyDisplay() {
    const sosBtn = document.getElementById('sosButton');
    const sosStatus = document.getElementById('sosStatus');
    const sosIcon = document.getElementById('sosStatusIcon');
    const sosText = document.getElementById('sosStatusText');

    if (sosBtn) {
        if (state.emergency.active) {
            sosBtn.classList.add('sos-active');
            sosBtn.innerHTML = '🚨 CANCEL SOS';
            sosBtn.onclick = () => sendCommand('emergency cancel');
        } else {
            sosBtn.classList.remove('sos-active');
            sosBtn.innerHTML = '🆘 EMERGENCY SOS';
            sosBtn.onclick = () => activateSOS();
        }
    }

    if (sosStatus) {
        if (state.emergency.active) {
            sosStatus.innerHTML = `
                <div class="sos-active-indicator">
                    🚨 SOS ACTIVE ${state.emergency.type ? `- ${state.emergency.type}` : ''}
                </div>
                <button class="btn btn-danger" onclick="sendCommand('emergency cancel')">Cancel SOS</button>
            `;
        } else {
            sosStatus.innerHTML = `
                <p class="text-muted">Emergency SOS ready</p>
                <div class="sos-types">
                    <button class="btn btn-danger" onclick="sendCommand('sos medical')">🏥 Medical</button>
                    <button class="btn btn-danger" onclick="sendCommand('sos fire')">🔥 Fire</button>
                    <button class="btn btn-danger" onclick="sendCommand('sos rescue')">🆘 Rescue</button>
                </div>
            `;
        }
    }

    if (sosIcon) sosIcon.textContent = state.emergency.active ? '🚨' : '🆘';
    if (sosText) {
        sosText.textContent = state.emergency.active ? 'ACTIVE' : 'Ready';
        sosText.style.color = state.emergency.active ? 'var(--danger-color)' : 'inherit';
    }
}

function updateDTNDisplay() {
    const container = document.getElementById('dtnStatus');
    if (!container) return;

    container.innerHTML = `
        <div class="dtn-stats">
            <span>Pending: ${state.dtn.stats.pending}</span>
            <span>Delivered: ${state.dtn.stats.delivered}</span>
            <span>Created: ${state.dtn.stats.created}</span>
        </div>
    `;
}

function updateVersionDisplay() {
    const versionInfo = document.getElementById('versionInfo');
    if (versionInfo) {
        const mismatchClass = state.versionMismatch ? 'version-mismatch' : 'version-match';
        versionInfo.innerHTML = `
            <div class="version-row ${mismatchClass}">
                <span>Web Client:</span> <code>${WEB_CLIENT_VERSION}</code>
            </div>
            <div class="version-row ${mismatchClass}">
                <span>Firmware:</span> <code>${state.firmwareVersion || 'Unknown'}</code>
            </div>
            ${state.versionMismatch ? '<div class="version-warning">⚠️ Version mismatch - some features may not work correctly</div>' : '<div class="version-ok">✅ Versions match</div>'}
        `;
    }

    setElementText('aboutVersion', WEB_CLIENT_VERSION);
    setElementText('aboutFirmware', state.firmwareVersion || 'Unknown');
}

function updateKnownNodesList() {
    const list = document.getElementById('knownNodesList');
    if (!list) return;

    const nodeNameInput = document.getElementById('nodeNameInput');
    if (nodeNameInput && state.nodeName && !nodeNameInput.value) {
        nodeNameInput.value = state.nodeName;
    }

    if (state.nodeNames.size === 0) {
        list.innerHTML = '<p class="text-muted">No named nodes</p>';
        return;
    }

    let html = '<div class="known-nodes">';
    for (const [addr, name] of state.nodeNames) {
        const isLocal = addr === state.nodeAddress;
        html += `
            <div class="known-node-item ${isLocal ? 'local' : ''}">
                <span class="known-node-name">${escapeHtml(name)}</span>
                ${isLocal ? '<span class="badge badge-success">This Node</span>' : ''}
            </div>
        `;
    }
    html += '</div>';
    list.innerHTML = html;
}

function updateNeighborGrid() {
    const grid = document.getElementById('neighborsGrid');
    if (!grid) return;

    if (state.neighbors.size === 0) {
        grid.innerHTML = `
            <div class="empty-state">
                <div class="empty-icon">🔍</div>
                <p>No neighbors discovered yet</p>
                <p class="text-muted">Waiting for beacon broadcasts...</p>
            </div>
        `;
        return;
    }

    grid.innerHTML = '';
    for (const [addr, neighbor] of state.neighbors) {
        const quality = calculateSignalQuality(neighbor.rssi, neighbor.snr);
        // Get friendly name - never show raw hex to user
        let displayName = neighbor.name || state.nodeNames.get(addr);
        if (!displayName) {
            // Generate friendly name from last 4 hex chars
            displayName = 'Node-' + addr.slice(-4).toUpperCase();
        }
        const card = document.createElement('div');
        card.className = 'neighbor-card';
        card.innerHTML = `
            <div class="neighbor-header">
                <span class="neighbor-icon">📡</span>
                <span class="neighbor-name">${escapeHtml(displayName)}</span>
            </div>
            <div class="neighbor-stats">
                <div class="neighbor-stat">
                    <span class="label">RSSI</span>
                    <span class="value">${neighbor.rssi} dBm</span>
                </div>
                <div class="neighbor-stat">
                    <span class="label">SNR</span>
                    <span class="value">${neighbor.snr} dB</span>
                </div>
                <div class="neighbor-stat">
                    <span class="label">Quality</span>
                    <span class="signal-indicator signal-${quality}">${quality}</span>
                </div>
                <div class="neighbor-stat">
                    <span class="label">Age</span>
                    <span class="value">${neighbor.age}s</span>
                </div>
            </div>
            <div class="neighbor-actions">
                <button class="btn btn-small btn-primary" onclick="sendMessageTo('${displayName}')">Message</button>
                <button class="btn btn-small btn-secondary" onclick="establishLink('${displayName}')">Link</button>
            </div>
        `;
        grid.appendChild(card);
    }

    // Update quick destination buttons in Messages tab
    updateQuickDestinations();
    // Update link target dropdown
    updateLinkTargetDropdown();
}

function updateRoutingTable() {
    const tbody = document.getElementById('routesTableBody');
    if (!tbody) return;

    if (state.routes.size === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="text-center">No routes</td></tr>';
        return;
    }

    tbody.innerHTML = '';
    for (const [dest, route] of state.routes) {
        const row = document.createElement('tr');
        const destName = getDisplayName(route.destination);
        const nextHopName = getDisplayName(route.nextHop);

        row.innerHTML = `
            <td><strong>${escapeHtml(destName)}</strong></td>
            <td>${escapeHtml(nextHopName)}</td>
            <td>${route.hops}</td>
            <td>
                <div class="route-quality-bar"><div class="route-quality-fill" style="width: ${route.quality}%"></div></div>
                ${route.quality}%
            </td>
            <td>${route.age}s</td>
            <td><button class="btn btn-small btn-primary" onclick="sendMessageTo('${escapeHtml(destName)}')">Send</button></td>
        `;
        tbody.appendChild(row);
    }
}

// =============================================================================
// Network Graph (D3.js)
// =============================================================================

let graphSimulation = null;

function initNetworkGraph() {
    const svg = d3.select('#networkGraph');
    if (!svg.node()) return;

    const container = svg.node().parentElement;
    const width = container.clientWidth || 800;
    const height = 500;

    svg.attr('width', width).attr('height', height);

    graphSimulation = d3.forceSimulation()
        .force('link', d3.forceLink().id(d => d.id).distance(150))
        .force('charge', d3.forceManyBody().strength(-400))
        .force('center', d3.forceCenter(width / 2, height / 2))
        .force('collision', d3.forceCollide().radius(40));

    updateNetworkGraph();
}

function updateNetworkGraph() {
    const svg = d3.select('#networkGraph');
    if (!svg.node() || !state.nodeAddress) return;

    svg.selectAll('*').remove();

    // Always use friendly names for display
    const selfName = state.nodeName || getDisplayName(state.nodeAddress);
    const nodes = [{ id: state.nodeAddress, type: 'self', name: selfName }];
    const links = [];

    for (const [addr, neighbor] of state.neighbors) {
        const friendlyName = getDisplayName(addr);
        nodes.push({ id: addr, type: 'neighbor', data: neighbor, name: friendlyName });
        links.push({ source: state.nodeAddress, target: addr, type: 'neighbor', rssi: neighbor.rssi });
    }

    for (const [dest, route] of state.routes) {
        if (!nodes.find(n => n.id === dest)) {
            const friendlyName = getDisplayName(dest);
            nodes.push({ id: dest, type: 'remote', data: route, name: friendlyName });
        }
        if (nodes.find(n => n.id === route.nextHop)) {
            links.push({ source: route.nextHop, target: dest, type: 'route' });
        }
    }

    // Markers
    svg.append('defs').selectAll('marker')
        .data(['neighbor', 'route'])
        .enter().append('marker')
        .attr('id', d => `arrow-${d}`)
        .attr('viewBox', '0 -5 10 10')
        .attr('refX', 25)
        .attr('refY', 0)
        .attr('markerWidth', 6)
        .attr('markerHeight', 6)
        .attr('orient', 'auto')
        .append('path')
        .attr('d', 'M0,-5L10,0L0,5')
        .attr('fill', d => d === 'neighbor' ? '#4CAF50' : '#2196F3');

    // Links
    const link = svg.append('g')
        .selectAll('line')
        .data(links)
        .enter().append('line')
        .attr('class', d => `graph-link link-${d.type}`)
        .attr('stroke-width', d => d.rssi ? Math.max(1, 5 + d.rssi/20) : 2);

    // Nodes
    const node = svg.append('g')
        .selectAll('g')
        .data(nodes)
        .enter().append('g')
        .attr('class', 'graph-node')
        .call(d3.drag()
            .on('start', dragStarted)
            .on('drag', dragged)
            .on('end', dragEnded));

    node.append('circle')
        .attr('r', 20)
        .attr('class', d => `node-${d.type}`);

    node.append('text')
        .text(d => d.name)  // Always use friendly name (already set above)
        .attr('text-anchor', 'middle')
        .attr('dy', 35)
        .attr('class', 'node-label');

    node.append('text')
        .text(d => d.type === 'self' ? '📡' : d.type === 'neighbor' ? '📶' : '🔀')
        .attr('text-anchor', 'middle')
        .attr('dy', 5)
        .style('font-size', '20px');

    node.append('title')
        .text(d => {
            // Tooltip shows friendly name only, no hex
            if (d.type === 'self') return `This Node\n${d.name}`;
            if (d.type === 'neighbor') return `Neighbor: ${d.name}\nRSSI: ${d.data.rssi} dBm\nSNR: ${d.data.snr} dB`;
            return `Remote: ${d.name}\nHops: ${d.data.hops}`;
        });

    setElementText('graphNodeCount', nodes.length);
    setElementText('graphLinkCount', links.length);

    if (graphSimulation) {
        graphSimulation.nodes(nodes);
        graphSimulation.force('link').links(links);
        graphSimulation.alpha(1).restart();

        graphSimulation.on('tick', () => {
            link.attr('x1', d => d.source.x).attr('y1', d => d.source.y)
                .attr('x2', d => d.target.x).attr('y2', d => d.target.y);
            node.attr('transform', d => `translate(${d.x},${d.y})`);
        });
    }
}

function dragStarted(event, d) {
    if (!event.active && graphSimulation) graphSimulation.alphaTarget(0.3).restart();
    d.fx = d.x;
    d.fy = d.y;
}

function dragged(event, d) {
    d.fx = event.x;
    d.fy = event.y;
}

function dragEnded(event, d) {
    if (!event.active && graphSimulation) graphSimulation.alphaTarget(0);
    d.fx = null;
    d.fy = null;
}

// =============================================================================
// Charts (Chart.js)
// =============================================================================

function initCharts() {
    const packetCtx = document.getElementById('packetChart');
    if (packetCtx) {
        state.charts.packetActivity = new Chart(packetCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    { label: 'Received', data: [], borderColor: '#4CAF50', tension: 0.4 },
                    { label: 'Sent', data: [], borderColor: '#2196F3', tension: 0.4 }
                ]
            },
            options: { responsive: true, maintainAspectRatio: false }
        });
    }

    const signalCtx = document.getElementById('signalChart');
    if (signalCtx) {
        state.charts.signalQuality = new Chart(signalCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    { label: 'RSSI (dBm)', data: [], borderColor: '#FF9800', yAxisID: 'y', tension: 0.4 },
                    { label: 'SNR (dB)', data: [], borderColor: '#9C27B0', yAxisID: 'y1', tension: 0.4 }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    y: { position: 'left', title: { display: true, text: 'RSSI' } },
                    y1: { position: 'right', title: { display: true, text: 'SNR' }, grid: { drawOnChartArea: false } }
                }
            }
        });
    }
}

function updateCharts() {
    const now = new Date().toLocaleTimeString();

    if (state.charts.packetActivity) {
        const chart = state.charts.packetActivity;
        chart.data.labels.push(now);
        chart.data.datasets[0].data.push(state.stats.packetsReceived);
        chart.data.datasets[1].data.push(state.stats.packetsSent);
        if (chart.data.labels.length > 20) {
            chart.data.labels.shift();
            chart.data.datasets.forEach(ds => ds.data.shift());
        }
        chart.update();
    }

    if (state.charts.signalQuality) {
        const chart = state.charts.signalQuality;
        chart.data.labels.push(now);
        chart.data.datasets[0].data.push(state.stats.rssi);
        chart.data.datasets[1].data.push(state.stats.snr);
        if (chart.data.labels.length > 20) {
            chart.data.labels.shift();
            chart.data.datasets.forEach(ds => ds.data.shift());
        }
        chart.update();
    }
}

// =============================================================================
// GPS Map (Leaflet)
// =============================================================================

let gpsMap = null;
let gpsMarker = null;

function initGPSMap() {
    const mapContainer = document.getElementById('map');
    if (!mapContainer) return;

    gpsMap = L.map('map').setView([39.8283, -98.5795], 4);
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '© OpenStreetMap'
    }).addTo(gpsMap);
}

function updateGPSMap() {
    if (!gpsMap || !state.gps.fix) return;

    const { latitude: lat, longitude: lon } = state.gps;
    if (!lat || !lon) return;

    if (gpsMarker) {
        gpsMarker.setLatLng([lat, lon]);
    } else {
        gpsMarker = L.marker([lat, lon]).addTo(gpsMap);
    }

    gpsMarker.bindPopup(`
        <b>${state.nodeName || 'This Node'}</b><br>
        Lat: ${lat.toFixed(6)}<br>
        Lon: ${lon.toFixed(6)}<br>
        Alt: ${state.gps.altitude}m<br>
        Sats: ${state.gps.satellites}
    `);

    gpsMap.setView([lat, lon], 15);
}

// =============================================================================
// Helper Functions
// =============================================================================

function setElementText(id, text) {
    const el = document.getElementById(id);
    if (el) el.textContent = text;
}

/**
 * Get friendly display name for an address - NEVER returns hex
 * @param {string} addr - Hex address like 0xABCD1234
 * @returns {string} Friendly name like "Alpha" or "Node-1234"
 */
function getDisplayName(addr) {
    // First check if we have a stored name
    const storedName = state.nodeNames.get(addr);
    if (storedName) return storedName;

    // Generate friendly name from last 4 hex digits
    if (addr && addr.startsWith('0x') && addr.length >= 6) {
        return 'Node-' + addr.slice(-4).toUpperCase();
    }

    // Fallback - should rarely happen
    return addr || 'Unknown';
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function calculateSignalQuality(rssi, snr) {
    if (rssi > -70 && snr > 5) return 'excellent';
    if (rssi > -90 && snr > 0) return 'good';
    if (rssi > -110 && snr > -5) return 'fair';
    return 'poor';
}

function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;

    if (days > 0) return `${days}d ${hours}h ${mins}m`;
    if (hours > 0) return `${hours}h ${mins}m ${secs}s`;
    if (mins > 0) return `${mins}m ${secs}s`;
    return `${secs}s`;
}

function showToast(message, type = 'info') {
    const container = document.getElementById('toastContainer');
    if (!container) return;

    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.textContent = message;

    container.appendChild(toast);
    setTimeout(() => toast.classList.add('show'), 10);
    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => container.removeChild(toast), 300);
    }, 3000);
}

// Console log level filter state
let consoleLogLevel = 'all'; // 'all', 'messages', 'network', 'errors'
let consoleUserScrolled = false;

// Spam filter - messages to suppress entirely
const spamPatterns = [
    /Time source updated/i,
    /TIME.*updated/i,
    /stratum.*quality/i,
    /^CRYSTAL/,
    /^SYNCED/,
    /^\s*$/,  // Empty lines
    /^={3,}$/,  // Separator lines
];

// Rate limiting for repeated messages
const recentMessages = new Map();
const MESSAGE_RATE_LIMIT_MS = 1000;  // Don't show same message more than once per second

function addConsoleMessage(message, type = 'output') {
    const consoleEl = document.getElementById('console');
    if (!consoleEl) return;

    // Filter out spam messages
    for (const pattern of spamPatterns) {
        if (pattern.test(message)) return;
    }

    // Rate limit repeated messages
    const msgKey = message.substring(0, 50);  // Use first 50 chars as key
    const now = Date.now();
    const lastSeen = recentMessages.get(msgKey);
    if (lastSeen && (now - lastSeen) < MESSAGE_RATE_LIMIT_MS) {
        return;  // Skip duplicate within rate limit window
    }
    recentMessages.set(msgKey, now);

    // Clean up old entries periodically
    if (recentMessages.size > 100) {
        for (const [key, time] of recentMessages) {
            if (now - time > 5000) recentMessages.delete(key);
        }
    }

    // Determine log category for filtering
    let category = 'debug';
    if (type === 'error') category = 'error';
    else if (type === 'warning') category = 'warning';
    else if (type === 'success') category = 'success';
    else if (type === 'command') category = 'command';
    else if (message.includes('MESSAGE') || message.includes('from 0x')) category = 'message';
    else if (message.includes('[MESH]') || message.includes('[RADIO]') || message.includes('Neighbor') || message.includes('Route')) category = 'network';
    else if (message.startsWith('>') || type === 'command') category = 'command';

    // Apply filter
    if (consoleLogLevel !== 'all') {
        if (consoleLogLevel === 'messages' && category !== 'message' && category !== 'command') return;
        if (consoleLogLevel === 'network' && category !== 'network' && category !== 'command') return;
        if (consoleLogLevel === 'errors' && category !== 'error' && category !== 'warning' && category !== 'command') return;
    }

    // Check if user is scrolled up (not at bottom)
    const wasAtBottom = consoleEl.scrollHeight - consoleEl.scrollTop <= consoleEl.clientHeight + 50;

    const line = document.createElement('div');
    line.className = `console-line console-${type}`;
    line.dataset.category = category;

    // Clean, simple format: just timestamp and message
    const timestamp = new Date().toLocaleTimeString('en-US', { hour12: false });
    line.textContent = `[${timestamp}] ${message}`;

    consoleEl.appendChild(line);

    // Only auto-scroll if user was already at bottom
    if (wasAtBottom && !consoleUserScrolled) {
        consoleEl.scrollTop = consoleEl.scrollHeight;
    }

    // Keep console from growing too large
    while (consoleEl.children.length > 300) {
        consoleEl.removeChild(consoleEl.firstChild);
    }
}

function setConsoleFilter(level) {
    consoleLogLevel = level;

    // Update filter button states
    document.querySelectorAll('.console-filter-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.filter === level);
    });

    // Re-filter existing messages
    const consoleEl = document.getElementById('console');
    if (!consoleEl) return;

    consoleEl.querySelectorAll('.console-line').forEach(line => {
        const cat = line.dataset.category;
        let show = true;

        if (level !== 'all') {
            if (level === 'messages' && cat !== 'message' && cat !== 'command') show = false;
            if (level === 'network' && cat !== 'network' && cat !== 'command') show = false;
            if (level === 'errors' && cat !== 'error' && cat !== 'warning' && cat !== 'command') show = false;
        }

        line.style.display = show ? '' : 'none';
    });
}

// =============================================================================
// Action Functions
// =============================================================================

function establishLink(addr) {
    const name = state.nodeNames.get(addr) || addr;
    sendCommand(`link ${name}`);
    showToast(`Requesting link to ${name}...`, 'info');
}

// Establish link from the UI (dropdown or text input)
function establishLinkFromUI() {
    const select = document.getElementById('linkTargetSelect');
    const input = document.getElementById('linkTargetInput');
    const forwardSecrecy = document.getElementById('linkForwardSecrecy')?.checked;

    let target = select?.value || input?.value?.trim();

    if (!target) {
        showToast('Please select a neighbor or enter an address', 'error');
        return;
    }

    // Use forward secrecy command if enabled
    if (forwardSecrecy) {
        sendCommand(`link ${target}`);
    } else {
        sendCommand(`link basic ${target}`);
    }

    showToast(`Establishing secure link to ${target}...`, 'info');

    // Clear inputs
    if (select) select.value = '';
    if (input) input.value = '';
}

// Update the link target dropdown with current neighbors
function updateLinkTargetDropdown() {
    const select = document.getElementById('linkTargetSelect');
    if (!select) return;

    // Keep the first option
    select.innerHTML = '<option value="">Select a neighbor...</option>';

    // Add all neighbors
    for (const [addr, neighbor] of state.neighbors) {
        const name = neighbor.name || state.nodeNames.get(addr) || ('Node-' + addr.slice(-4).toUpperCase());
        const option = document.createElement('option');
        option.value = name;
        option.textContent = `${name} (RSSI: ${neighbor.rssi} dBm)`;
        select.appendChild(option);
    }
}

function closeLink(addr) {
    const name = state.nodeNames.get(addr) || addr;
    sendCommand(`link close ${name}`);
}

// =============================================================================
// Channel Functions
// =============================================================================

function showChannelTab(tab) {
    const createPane = document.getElementById('createChannelPane');
    const joinPane = document.getElementById('joinChannelPane');
    const tabs = document.querySelectorAll('.channel-tab');

    tabs.forEach(t => t.classList.remove('active'));

    if (tab === 'create') {
        createPane?.classList.add('active');
        joinPane?.classList.remove('active');
        tabs[0]?.classList.add('active');
    } else {
        createPane?.classList.remove('active');
        joinPane?.classList.add('active');
        tabs[1]?.classList.add('active');
    }
}

function createChannel() {
    const name = document.getElementById('newGroupName')?.value.trim();
    const useCustomKey = document.querySelector('input[name="channelSecurity"][value="custom"]')?.checked;
    const customKey = document.getElementById('customChannelKey')?.value.trim();

    if (!name) {
        showToast('Please enter a channel name', 'error');
        return;
    }

    if (name.length > 16) {
        showToast('Channel name must be 16 characters or less', 'error');
        return;
    }

    if (useCustomKey) {
        if (!customKey || customKey.length !== 64 || !/^[0-9A-Fa-f]+$/.test(customKey)) {
            showToast('Custom key must be exactly 64 hex characters', 'error');
            return;
        }
        sendCommand(`group create ${name} ${customKey}`);
    } else {
        sendCommand(`group create ${name}`);
    }

    showToast(`Creating channel "${name}"...`, 'info');

    // Clear input
    document.getElementById('newGroupName').value = '';

    // Refresh group list
    setTimeout(() => sendCommand('group'), 500);
}

function joinChannel() {
    const name = document.getElementById('joinGroupName')?.value.trim();
    const key = document.getElementById('joinGroupKey')?.value.trim();

    if (!name || !key) {
        showToast('Please enter both channel name and key', 'error');
        return;
    }

    if (key.length !== 64 || !/^[0-9A-Fa-f]+$/.test(key)) {
        showToast('Channel key must be exactly 64 hex characters', 'error');
        return;
    }

    sendCommand(`group join ${name} ${key}`);
    showToast(`Joining channel "${name}"...`, 'info');

    // Clear inputs
    document.getElementById('joinGroupName').value = '';
    document.getElementById('joinGroupKey').value = '';

    // Refresh group list
    setTimeout(() => sendCommand('group'), 500);
}

function sendMessageTo(addr) {
    const destInput = document.getElementById('destAddress');
    if (destInput) {
        destInput.value = state.nodeNames.get(addr) || addr;
        switchTab('messagesTab');
        document.getElementById('messageText')?.focus();
    }
}

function sendGroupMessage(groupName) {
    const msg = prompt(`Message to group "${groupName}":`);
    if (msg) {
        sendCommand(`group send ${groupName} ${msg}`);
    }
}

function leaveGroup(groupName) {
    if (confirm(`Leave group "${groupName}"?`)) {
        sendCommand(`group leave ${groupName}`);
    }
}

function toggleADR() {
    sendCommand(state.adr.enabled ? 'adr off' : 'adr on');
    setTimeout(() => sendCommand('adr'), 500);
}

function activateSOS() {
    if (confirm('⚠️ ACTIVATE EMERGENCY SOS?\n\nThis will broadcast your location and emergency status to all nodes in range.')) {
        sendCommand('sos');
    }
}

function sendMessage() {
    const destInput = document.getElementById('destAddress');
    const msgInput = document.getElementById('messageText');

    let dest = destInput?.value.trim() || state.currentChat;
    const msg = msgInput?.value.trim();

    console.log(`[SEND] dest="${dest}" msg="${msg}" currentChat="${state.currentChat}"`);

    if (!msg) {
        showToast('Enter a message', 'error');
        return;
    }

    // Convert "broadcast" to hex address for firmware compatibility
    if (dest === 'broadcast') {
        dest = '0xFFFFFFFF';
    }

    // Send command to radio
    const cmd = `send ${dest} ${msg}`;
    console.log(`[SEND] Sending command: ${cmd}`);
    sendCommand(cmd);

    // Create message object
    const msgObj = {
        id: Date.now(),
        to: dest,
        content: msg,
        timestamp: new Date(),
        direction: 'outgoing',
        type: dest === 'broadcast' ? 'BROADCAST' : 'DIRECT'
    };

    // Add to messages array
    state.messages.push(msgObj);

    // Add to conversation
    const chatId = dest === 'broadcast' ? 'broadcast' : dest;
    if (!state.conversations.has(chatId)) {
        state.conversations.set(chatId, { messages: [] });
    }
    state.conversations.get(chatId).messages.push(msgObj);

    // Update UI with new bubble
    const messageList = document.getElementById('messagesList');
    if (messageList) {
        // Remove empty state if present
        const emptyState = messageList.querySelector('.chat-messages-empty');
        if (emptyState) emptyState.remove();

        // Add bubble
        const bubble = createMessageBubble(msgObj);
        messageList.appendChild(bubble);
        messageList.scrollTop = messageList.scrollHeight;
    }

    // Clear input
    msgInput.value = '';

    // Update counts and DM list
    updateMessageCount();
    updateDMList();

    showToast('Message sent', 'success');
}

/**
 * Add a received message to the messages list and state
 */
function addReceivedMessage(msg) {
    // Ensure message has required fields
    msg.direction = 'incoming';

    // Add to state
    state.messages.push(msg);

    // Determine which conversation this belongs to
    const chatId = msg.type === 'BROADCAST' ? 'broadcast' : msg.fromName;

    // Add to conversation
    if (!state.conversations.has(chatId)) {
        state.conversations.set(chatId, { messages: [] });
    }
    state.conversations.get(chatId).messages.push(msg);

    // Increment unread count if not viewing this conversation
    if (state.currentChat !== chatId) {
        const current = state.unreadCounts.get(chatId) || 0;
        state.unreadCounts.set(chatId, current + 1);
        updateUnreadBadge(chatId);
    }

    // Only update UI if we're viewing this conversation
    if (state.currentChat === chatId) {
        const messageList = document.getElementById('messagesList');
        if (messageList) {
            // Remove empty state if present
            const emptyState = messageList.querySelector('.chat-messages-empty');
            if (emptyState) emptyState.remove();

            // Add bubble
            const bubble = createMessageBubble(msg);
            messageList.appendChild(bubble);
            messageList.scrollTop = messageList.scrollHeight;
        }
    }

    // Update DM list to show new preview
    updateDMList();

    // Show toast notification
    showToast(`New message from ${msg.fromName}`, 'info');

    // Update message count
    updateMessageCount();
}

/**
 * Update the message count badge
 */
function updateMessageCount() {
    const countEl = document.getElementById('msgCount');
    if (countEl) {
        const count = state.messages.length;
        countEl.textContent = `${count} message${count !== 1 ? 's' : ''}`;
    }
}

/**
 * Update DM list in sidebar with current neighbors (Meshtastic-style)
 */
function updateDMList() {
    const container = document.getElementById('dmList');
    if (!container) return;

    if (state.neighbors.size === 0) {
        container.innerHTML = '<div class="chat-empty">No neighbors discovered</div>';
        return;
    }

    container.innerHTML = '';

    // Add a chat item for each neighbor
    state.neighbors.forEach((neighbor, addr) => {
        // Get friendly name
        let name = neighbor.name || state.nodeNames.get(addr);
        if (!name) {
            name = 'Node-' + addr.slice(-4).toUpperCase();
        }

        const chatId = name;
        const unreadCount = state.unreadCounts.get(chatId) || 0;
        const isActive = state.currentChat === chatId;

        // Get last message preview
        const conv = state.conversations.get(chatId);
        const lastMsg = conv?.messages?.[conv.messages.length - 1];
        const preview = lastMsg ? (lastMsg.content.substring(0, 30) + (lastMsg.content.length > 30 ? '...' : '')) : 'No messages';

        const item = document.createElement('div');
        item.className = `chat-item ${isActive ? 'active' : ''}`;
        item.dataset.chat = chatId;
        item.innerHTML = `
            <span class="chat-icon">💬</span>
            <div class="chat-info">
                <span class="chat-name">${escapeHtml(name)}</span>
                <span class="chat-preview">${escapeHtml(preview)}</span>
            </div>
            ${unreadCount > 0 ? `<span class="chat-unread">${unreadCount}</span>` : ''}
        `;
        item.addEventListener('click', () => selectConversation(chatId, name));
        container.appendChild(item);
    });
}

/**
 * Select a conversation (broadcast or DM)
 */
function selectConversation(chatId, displayName) {
    state.currentChat = chatId;

    // Update destination
    const destInput = document.getElementById('destAddress');
    if (destInput) {
        destInput.value = chatId === 'broadcast' ? 'broadcast' : chatId;
    }

    // Update header
    const headerIcon = document.getElementById('currentChatName')?.previousElementSibling;
    const headerName = document.getElementById('currentChatName');
    const headerStatus = document.getElementById('currentChatStatus');

    if (chatId === 'broadcast') {
        if (headerIcon) headerIcon.textContent = '📢';
        if (headerName) headerName.textContent = 'Broadcast';
        if (headerStatus) headerStatus.textContent = 'All nodes';
    } else {
        if (headerIcon) headerIcon.textContent = '💬';
        if (headerName) headerName.textContent = displayName || chatId;
        // Find neighbor info for status
        const neighbor = findNeighborByName(chatId);
        if (headerStatus) {
            headerStatus.textContent = neighbor ? `RSSI: ${neighbor.rssi} dBm` : 'Direct message';
        }
    }

    // Clear unread for this conversation
    state.unreadCounts.set(chatId, 0);

    // Update active states in sidebar
    document.querySelectorAll('.chat-item').forEach(item => {
        item.classList.toggle('active', item.dataset.chat === chatId);
    });

    // Update unread badge
    updateUnreadBadge(chatId);

    // Render messages for this conversation
    renderConversationMessages(chatId);

    // Focus message input
    document.getElementById('messageText')?.focus();
}

/**
 * Find neighbor by friendly name
 */
function findNeighborByName(name) {
    for (const [addr, neighbor] of state.neighbors) {
        const neighborName = neighbor.name || state.nodeNames.get(addr) || ('Node-' + addr.slice(-4).toUpperCase());
        if (neighborName === name) {
            return neighbor;
        }
    }
    return null;
}

/**
 * Render messages for a specific conversation
 */
function renderConversationMessages(chatId) {
    const messageList = document.getElementById('messagesList');
    if (!messageList) return;

    // Get messages for this conversation
    const conv = state.conversations.get(chatId);
    const messages = conv?.messages || [];

    // Also include sent messages to this destination
    const allMessages = state.messages.filter(msg => {
        if (chatId === 'broadcast') {
            return msg.type === 'BROADCAST' || msg.to === 'broadcast';
        } else {
            return msg.fromName === chatId || msg.to === chatId;
        }
    });

    if (allMessages.length === 0) {
        messageList.innerHTML = `
            <div class="chat-messages-empty">
                <div class="empty-icon">💬</div>
                <h3>No Messages</h3>
                <p>${chatId === 'broadcast' ? 'Broadcast messages will appear here' : `Start a conversation with ${chatId}`}</p>
            </div>
        `;
        return;
    }

    messageList.innerHTML = '';

    allMessages.forEach(msg => {
        const bubble = createMessageBubble(msg);
        messageList.appendChild(bubble);
    });

    messageList.scrollTop = messageList.scrollHeight;
}

/**
 * Create a message bubble element (Meshtastic-style)
 */
function createMessageBubble(msg) {
    const isOutgoing = msg.direction === 'outgoing' || msg.to;
    const isBroadcast = msg.type === 'BROADCAST' || msg.to === 'broadcast';

    const bubble = document.createElement('div');
    bubble.className = `msg-bubble ${isOutgoing ? 'msg-outgoing' : 'msg-incoming'} ${isBroadcast ? 'msg-broadcast' : ''}`;

    const time = msg.timestamp ? msg.timestamp.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) : '';

    bubble.innerHTML = `
        ${!isOutgoing ? `<div class="msg-sender">${escapeHtml(msg.fromName || msg.from || 'Unknown')}</div>` : ''}
        <div class="msg-content">${escapeHtml(msg.content)}</div>
        <div class="msg-meta">
            <span class="msg-time">${time}</span>
            ${isOutgoing ? '<span class="msg-status">✓</span>' : ''}
            ${isBroadcast ? '<span class="msg-badge">📢</span>' : ''}
        </div>
    `;

    return bubble;
}

/**
 * Update unread badge for a conversation
 */
function updateUnreadBadge(chatId) {
    if (chatId === 'broadcast') {
        const badge = document.getElementById('broadcastUnread');
        const count = state.unreadCounts.get('broadcast') || 0;
        if (badge) {
            badge.textContent = count;
            badge.style.display = count > 0 ? 'inline' : 'none';
        }
    }
    // Update DM list to reflect unread counts
    updateDMList();
}

// Legacy function name for compatibility
function updateQuickDestinations() {
    updateDMList();
}

function switchTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
    document.querySelectorAll('.nav-item').forEach(item => item.classList.remove('active'));

    const targetTab = document.getElementById(tabId);
    if (targetTab) targetTab.classList.add('active');

    document.querySelectorAll('.nav-item').forEach(item => {
        if (item.getAttribute('data-tab') + 'Tab' === tabId) {
            item.classList.add('active');
        }
    });

    if (tabId === 'mapTab' && gpsMap) setTimeout(() => gpsMap.invalidateSize(), 100);
    if (tabId === 'networkTab') setTimeout(() => updateNetworkGraph(), 100);
}

// =============================================================================
// Event Handlers
// =============================================================================

document.addEventListener('DOMContentLoaded', () => {
    // Connect button
    document.getElementById('connectBtn')?.addEventListener('click', () => {
        if (state.connected) {
            disconnectSerial();
        } else {
            connectSerial();
        }
    });

    // Tab navigation
    document.querySelectorAll('.nav-item').forEach(item => {
        item.addEventListener('click', (e) => {
            e.preventDefault();
            switchTab(item.getAttribute('data-tab') + 'Tab');
        });
    });

    // Console input
    const consoleInput = document.getElementById('consoleInput');
    consoleInput?.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            const cmd = consoleInput.value.trim();
            if (cmd) {
                sendCommand(cmd);
                state.consoleHistory.push(cmd);
                state.historyIndex = state.consoleHistory.length;
                consoleInput.value = '';
            }
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            if (state.historyIndex > 0) {
                state.historyIndex--;
                consoleInput.value = state.consoleHistory[state.historyIndex];
            }
        } else if (e.key === 'ArrowDown') {
            e.preventDefault();
            if (state.historyIndex < state.consoleHistory.length - 1) {
                state.historyIndex++;
                consoleInput.value = state.consoleHistory[state.historyIndex];
            } else {
                state.historyIndex = state.consoleHistory.length;
                consoleInput.value = '';
            }
        }
    });

    // Send command button
    document.getElementById('sendCmdBtn')?.addEventListener('click', () => {
        const input = document.getElementById('consoleInput');
        if (input?.value.trim()) {
            sendCommand(input.value.trim());
            input.value = '';
        }
    });

    // Message send
    document.getElementById('sendBtn')?.addEventListener('click', sendMessage);
    document.getElementById('messageText')?.addEventListener('keydown', (e) => {
        // Enter to send (Shift+Enter for newline)
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            sendMessage();
        }
    });

    // Quick destination buttons
    document.querySelectorAll('.quick-dest-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const dest = btn.dataset.dest;
            const destInput = document.getElementById('destAddress');
            if (destInput && dest) {
                destInput.value = dest;
                // Update active state
                document.querySelectorAll('.quick-dest-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
            }
        });
    });

    // Clear messages button
    document.getElementById('clearMessages')?.addEventListener('click', () => {
        state.messages = [];
        state.conversations.clear();
        state.unreadCounts.clear();
        const messageList = document.getElementById('messagesList');
        if (messageList) {
            messageList.innerHTML = `
                <div class="chat-messages-empty">
                    <div class="empty-icon">💬</div>
                    <h3>No Messages</h3>
                    <p>Messages will appear here</p>
                </div>
            `;
        }
        updateMessageCount();
        updateDMList();
        showToast('Messages cleared', 'info');
    });

    // Broadcast channel click
    document.querySelector('.chat-item[data-chat="broadcast"]')?.addEventListener('click', () => {
        selectConversation('broadcast', 'Broadcast');
    });

    // Refresh button
    document.getElementById('refreshBtn')?.addEventListener('click', queryAllStatus);

    // Clear console
    document.getElementById('clearConsole')?.addEventListener('click', () => {
        const consoleEl = document.getElementById('console');
        if (consoleEl) consoleEl.innerHTML = '';
    });

    // Node naming
    document.getElementById('setNodeNameBtn')?.addEventListener('click', () => {
        const name = document.getElementById('nodeNameInput')?.value.trim();
        if (name && name.length <= 16) {
            sendCommand(`name set ${name}`);
            setTimeout(() => sendCommand('name'), 500);
        } else {
            showToast('Name must be 1-16 characters', 'error');
        }
    });

    document.getElementById('addNodeNameBtn')?.addEventListener('click', () => {
        const addr = document.getElementById('newNodeAddr')?.value.trim();
        const name = document.getElementById('newNodeName')?.value.trim();
        if (addr && name) {
            sendCommand(`name add ${addr} ${name}`);
            document.getElementById('newNodeAddr').value = '';
            document.getElementById('newNodeName').value = '';
            setTimeout(() => sendCommand('name list'), 500);
        }
    });

    // Group/Channel creation - use the enhanced createChannel function
    document.getElementById('createGroupBtn')?.addEventListener('click', createChannel);

    // Channel security radio buttons - show/hide custom key input
    document.querySelectorAll('input[name="channelSecurity"]').forEach(radio => {
        radio.addEventListener('change', (e) => {
            const customKeyGroup = document.getElementById('customKeyGroup');
            if (customKeyGroup) {
                customKeyGroup.style.display = e.target.value === 'custom' ? 'block' : 'none';
            }
        });
    });

    // Initialize visualizations
    initNetworkGraph();
    initCharts();
    initGPSMap();

    // Auto-refresh
    setInterval(() => {
        if (state.connected) {
            sendCommand('status');
            updateCharts();
        }
    }, 5000);

    // Periodic neighbor/route refresh
    setInterval(() => {
        if (state.connected) {
            sendCommand('neighbors');
            sendCommand('routes');
        }
    }, 10000);
});

// Console scroll detection - track when user scrolls up
document.addEventListener('DOMContentLoaded', () => {
    const consoleEl = document.getElementById('console');
    if (consoleEl) {
        consoleEl.addEventListener('scroll', () => {
            const atBottom = consoleEl.scrollHeight - consoleEl.scrollTop <= consoleEl.clientHeight + 50;
            consoleUserScrolled = !atBottom;
            const indicator = document.getElementById('scrollIndicator');
            if (indicator) {
                indicator.style.display = consoleUserScrolled ? 'block' : 'none';
            }
        });
    }
});

// Scroll console to bottom (for button click)
function scrollConsoleToBottom() {
    const consoleEl = document.getElementById('console');
    if (consoleEl) {
        consoleEl.scrollTop = consoleEl.scrollHeight;
        consoleUserScrolled = false;
        const indicator = document.getElementById('scrollIndicator');
        if (indicator) indicator.style.display = 'none';
    }
}

// =============================================================================
// TDMA/MAC Layer Functions
// =============================================================================

// TDMA/MAC state
const macState = {
    enabled: false,
    currentFrame: 0,
    currentSlot: 0,
    timeSource: 'CRYSTAL',
    stratum: 15,
    tdmaTx: 0,
    csmaTx: 0,
    collisions: 0,
    ccaBusy: 0,
    timeSyncs: 0,
    slots: Array(10).fill().map((_, i) => ({
        type: i === 0 ? 'beacon' : 'free',
        owner: null
    }))
};

// Set time from host computer (uses serial timestamp)
function setTimeFromHost() {
    const timestamp = Math.floor(Date.now() / 1000);
    sendCommand(`time ${timestamp}`);
    addConsoleMessage(`Setting time to ${timestamp} (${new Date().toISOString()})`, 'info');
    showToast('Time sync sent to device', 'info');
}

// Update TDMA/MAC display from parsed data
function updateTDMADisplay() {
    // Update overview stats
    setElementText('tdmaFrame', macState.currentFrame);
    setElementText('tdmaSlot', macState.currentSlot);
    setElementText('timeSource', macState.timeSource);
    setElementText('timeQuality', `Stratum ${macState.stratum}`);

    // Update statistics
    setElementText('tdmaTxCount', macState.tdmaTx);
    setElementText('csmaTxCount', macState.csmaTx);
    setElementText('collisionCount', macState.collisions);
    setElementText('ccaBusyCount', macState.ccaBusy);
    setElementText('timeSyncCount', macState.timeSyncs);
    setElementText('timeStratum', macState.stratum);

    // Update slot visualization
    updateSlotVisualization();

    // Update time sync status
    updateTimeSyncStatus();
}

function updateSlotVisualization() {
    const slots = document.querySelectorAll('#slotVisualization .slot');
    if (!slots.length) return;

    slots.forEach((slotEl, i) => {
        // Remove all state classes
        slotEl.classList.remove('slot-current', 'slot-beacon', 'slot-reserved', 'slot-peer', 'slot-free');

        const slotData = macState.slots[i] || { type: 'free', owner: null };

        // Add type class
        switch (slotData.type) {
            case 'beacon':
                slotEl.classList.add('slot-beacon');
                break;
            case 'reserved':
                slotEl.classList.add('slot-reserved');
                break;
            case 'peer':
                slotEl.classList.add('slot-peer');
                break;
            default:
                slotEl.classList.add('slot-free');
        }

        // Highlight current slot
        if (i === macState.currentSlot) {
            slotEl.classList.add('slot-current');
        }

        // Update tooltip
        const typeText = slotData.owner ? `${slotData.type.toUpperCase()} (${slotData.owner})` : slotData.type.toUpperCase();
        slotEl.title = `Slot ${i}: ${typeText}`;
    });
}

function updateTimeSyncStatus() {
    const statusEl = document.getElementById('timeSyncStatus');
    if (!statusEl) return;

    // Remove all state classes
    statusEl.classList.remove('synced', 'gps', 'ntp', 'serial');

    const textEl = statusEl.querySelector('.status-text');
    let statusClass = '';
    let statusText = 'Not synced';

    switch (macState.timeSource.toUpperCase()) {
        case 'GPS':
            statusClass = 'gps';
            statusText = `GPS Time Master (Stratum ${macState.stratum})`;
            break;
        case 'NTP':
            statusClass = 'ntp';
            statusText = `NTP Synced (Stratum ${macState.stratum})`;
            break;
        case 'SERIAL':
            statusClass = 'serial';
            statusText = `Serial Time (Stratum ${macState.stratum})`;
            break;
        case 'SYNCED':
            statusClass = 'synced';
            statusText = `Network Synced (Stratum ${macState.stratum})`;
            break;
        default:
            statusText = `Crystal Only (Stratum ${macState.stratum})`;
    }

    if (statusClass) {
        statusEl.classList.add(statusClass);
    }
    if (textEl) {
        textEl.textContent = statusText;
    }
}

// Parse MAC status output
function parseMACStatusLine(line) {
    let match;
    console.log('[MAC PARSE]', line);

    // TDMA Enabled: YES/NO (firmware format)
    if (line.includes('TDMA Enabled:')) {
        macState.enabled = line.includes('YES');
        console.log('[MAC] TDMA Enabled:', macState.enabled);
    }

    // Current Frame: 12345 (firmware format)
    match = line.match(/Current Frame:\s*(\d+)/);
    if (match) {
        macState.currentFrame = parseInt(match[1]);
    }

    // Current Slot: 5/10 (firmware format)
    match = line.match(/Current Slot:\s*(\d+)/);
    if (match) {
        macState.currentSlot = parseInt(match[1]);
    }

    // Time Source: GPS (stratum 1, quality 100%) - firmware format
    match = line.match(/Time Source:\s*(\w+)\s*\(stratum\s*(\d+)/);
    if (match) {
        macState.timeSource = match[1];
        macState.stratum = parseInt(match[2]);
    }

    // Also support simple format: Time Source: GPS
    if (!match) {
        match = line.match(/Time Source:\s*(\w+)/);
        if (match) {
            macState.timeSource = match[1];
        }
    }

    // Stratum: 1 (standalone format)
    match = line.match(/stratum[:\s]+(\d+)/i);
    if (match) {
        macState.stratum = parseInt(match[1]);
    }

    // Statistics: TDMA TX: 50 (firmware format)
    match = line.match(/TDMA TX:\s*(\d+)/);
    if (match) {
        macState.tdmaTx = parseInt(match[1]);
    }

    // CSMA TX: 30 (firmware format)
    match = line.match(/CSMA TX:\s*(\d+)/);
    if (match) {
        macState.csmaTx = parseInt(match[1]);
    }

    // Collisions: 2
    match = line.match(/Collisions:\s*(\d+)/);
    if (match) {
        macState.collisions = parseInt(match[1]);
    }

    // CCA Busy: 5
    match = line.match(/CCA Busy:\s*(\d+)/);
    if (match) {
        macState.ccaBusy = parseInt(match[1]);
    }

    // Time Syncs: 10
    match = line.match(/Time Syncs:\s*(\d+)/);
    if (match) {
        macState.timeSyncs = parseInt(match[1]);
    }

    updateTDMADisplay();
}

// =============================================================================
// ARP / Neighbor Table Functions
// =============================================================================

// Update the ARP/Neighbor table display
function updateARPTable() {
    const tableBody = document.getElementById('arpTableBody');
    const signalMapNodes = document.getElementById('signalMapNodes');

    if (!tableBody) return;

    if (state.neighbors.size === 0) {
        tableBody.innerHTML = `
            <tr>
                <td colspan="9" class="text-center">
                    <div class="empty-state-inline">
                        <span>📡</span> No neighbors discovered
                    </div>
                </td>
            </tr>
        `;
        if (signalMapNodes) {
            signalMapNodes.innerHTML = '<p class="text-muted">Connect to see neighbor signal strengths</p>';
        }
        updateARPStats();
        return;
    }

    // Build table rows
    let tableHtml = '';
    let signalHtml = '';
    let totalRssi = 0;
    let latestSeen = null;

    for (const [addr, neighbor] of state.neighbors) {
        const name = getDisplayName(addr);
        const quality = getSignalQuality(neighbor.rssi);
        const qualityPercent = Math.min(100, Math.max(0, (neighbor.rssi + 120) * 2));
        const lastSeen = neighbor.lastSeen ? formatTimeAgo(neighbor.lastSeen) : '-';

        totalRssi += neighbor.rssi || 0;
        if (!latestSeen || (neighbor.lastSeen && neighbor.lastSeen > latestSeen)) {
            latestSeen = neighbor.lastSeen;
        }

        // Interface badge styling - show all available interfaces with preferred highlighted
        const interfaces = neighbor.interfaces || [neighbor.interface || 'LORA'];
        const preferred = neighbor.preferred || interfaces[0];
        const ifaceBadge = getInterfaceBadge(interfaces, preferred);

        // Build signal details tooltip showing each interface's signal
        let signalDetails = '';
        if (neighbor.signalInfo) {
            const details = Object.entries(neighbor.signalInfo).map(([iface, info]) => {
                if (iface === 'LAN') return `${iface}: connected`;
                return `${iface}: ${info.rssi}dBm${info.snr ? `/${info.snr}dB` : ''}`;
            }).join(', ');
            signalDetails = ` title="${details}"`;
        }

        tableHtml += `
            <tr>
                <td><strong>${name !== addr ? name : '-'}</strong></td>
                <td><code>${addr}</code></td>
                <td>${ifaceBadge}</td>
                <td class="${quality}"${signalDetails}>${neighbor.rssi || '-'} dBm</td>
                <td>${neighbor.snr || '-'} dB</td>
                <td>
                    <div class="quality-bar">
                        <div class="quality-bar-fill ${quality}" style="width: ${qualityPercent}%"></div>
                    </div>
                </td>
                <td>${lastSeen}</td>
                <td>${neighbor.packets || '-'}</td>
                <td class="arp-actions">
                    <button class="btn btn-secondary" onclick="sendCommand('send ${addr} ping')">Ping</button>
                </td>
            </tr>
        `;

        signalHtml += `
            <div class="signal-node ${quality}">
                <div class="node-name">${name !== addr ? name : 'Unknown'}</div>
                <div class="node-addr">${addr}</div>
                <div class="node-rssi">${neighbor.rssi || '-'} dBm</div>
                <div class="node-snr">SNR: ${neighbor.snr || '-'} dB</div>
                <div class="quality-bar">
                    <div class="quality-bar-fill ${quality}" style="width: ${qualityPercent}%"></div>
                </div>
            </div>
        `;
    }

    tableBody.innerHTML = tableHtml;

    if (signalMapNodes) {
        signalMapNodes.innerHTML = signalHtml;
    }

    updateARPStats(totalRssi, latestSeen);
}

function updateARPStats(totalRssi = 0, latestSeen = null) {
    setElementText('arpNeighborCount', state.neighbors.size);

    const avgRssi = state.neighbors.size > 0 ? Math.round(totalRssi / state.neighbors.size) : '-';
    setElementText('arpAvgRSSI', avgRssi !== '-' ? `${avgRssi} dBm` : '-');

    const lastBeacon = latestSeen ? formatTimeAgo(latestSeen) : '-';
    setElementText('arpLastSeen', lastBeacon);
}

function getSignalQuality(rssi) {
    if (!rssi) return 'poor';
    if (rssi > -70) return 'excellent';
    if (rssi > -85) return 'good';
    if (rssi > -100) return 'fair';
    return 'poor';
}

// Get interface badge HTML - can take single interface or array
function getInterfaceBadge(iface, preferred = null) {
    const colors = {
        'LORA': { bg: '#2196F3', text: 'white' },
        'BLE':  { bg: '#9C27B0', text: 'white' },
        'LAN':  { bg: '#4CAF50', text: 'white' },
        'WAN':  { bg: '#FF9800', text: 'white' },
        'UNK':  { bg: '#607D8B', text: 'white' }
    };

    // Handle array of interfaces
    if (Array.isArray(iface)) {
        if (iface.length === 0) {
            return getBadgeHtml('UNK', colors['UNK'], false);
        }
        return iface.map(i => {
            const upper = (i || 'UNK').toUpperCase();
            const style = colors[upper] || colors['UNK'];
            const isPreferred = preferred && upper === preferred.toUpperCase();
            return getBadgeHtml(upper, style, isPreferred);
        }).join(' ');
    }

    // Single interface
    const ifaceUpper = (iface || 'UNK').toUpperCase();
    const style = colors[ifaceUpper] || colors['UNK'];
    const isPreferred = preferred && ifaceUpper === preferred.toUpperCase();
    return getBadgeHtml(ifaceUpper, style, isPreferred);
}

function getBadgeHtml(label, style, isPreferred) {
    const border = isPreferred ? 'border:2px solid gold;box-shadow:0 0 4px gold;' : '';
    const star = isPreferred ? '★' : '';
    return `<span class="iface-badge" style="background:${style.bg};color:${style.text};padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600;${border}">${star}${label}</span>`;
}

function formatTimeAgo(timestamp) {
    if (!timestamp) return '-';
    const now = Date.now();
    const diff = now - timestamp;

    if (diff < 1000) return 'just now';
    if (diff < 60000) return `${Math.floor(diff / 1000)}s ago`;
    if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`;
    return `${Math.floor(diff / 3600000)}h ago`;
}

// =============================================================================
// Radio Status Display Functions
// =============================================================================

// Radio state
const radioState = {
    frequency: 915.0,
    txPower: 22,
    spreadingFactor: 10,
    channel: 0,
    rssi: -65,
    snr: 9,
    txPackets: 0,
    rxPackets: 0,
    crcErrors: 0,
    txErrors: 0,
    macMode: 'CSMA-CA',  // 'TDMA' or 'CSMA-CA'
    tdmaEnabled: false
};

function updateRadioDisplay() {
    // Update radio stats
    setElementText('radioFrequency', radioState.frequency.toFixed(1));
    setElementText('radioTxPower', radioState.txPower);
    setElementText('radioSF', `SF${radioState.spreadingFactor}`);
    setElementText('radioChannel', radioState.channel);

    // Update signal quality
    setElementText('radioRSSI', `${radioState.rssi} dBm`);
    setElementText('radioSNR', `${radioState.snr} dB`);

    // Update signal bars (RSSI: -120 is worst, -40 is best)
    const rssiPercent = Math.min(100, Math.max(0, (radioState.rssi + 120) * 1.25));
    const rssiBar = document.getElementById('rssiBarFill');
    if (rssiBar) rssiBar.style.width = `${rssiPercent}%`;

    // SNR: -20 is worst, +20 is best
    const snrPercent = Math.min(100, Math.max(0, (radioState.snr + 20) * 2.5));
    const snrBar = document.getElementById('snrBarFill');
    if (snrBar) snrBar.style.width = `${snrPercent}%`;

    // Update packet stats
    setElementText('radioTxPackets', radioState.txPackets);
    setElementText('radioRxPackets', radioState.rxPackets);
    setElementText('radioCrcErrors', radioState.crcErrors);
    setElementText('radioTxErrors', radioState.txErrors);

    // Update MAC mode display
    updateMACModeDisplay();
}

function updateMACModeDisplay() {
    const modeIcon = document.querySelector('.mac-mode-indicator .mode-icon');
    const modeName = document.getElementById('macModeName');
    const modeDesc = document.getElementById('macModeDesc');
    const modeStatus = document.getElementById('macModeStatus');
    const tdmaStatusText = document.getElementById('tdmaStatusText');
    const timeQualityText = document.getElementById('timeQualityText');
    const assignedSlotText = document.getElementById('assignedSlotText');

    const isTDMA = macState.enabled && macState.stratum < 15;

    if (isTDMA) {
        // TDMA Mode active
        if (modeIcon) modeIcon.textContent = '⏱️';
        if (modeName) modeName.textContent = 'TDMA';
        if (modeDesc) modeDesc.textContent = 'Time Division Multiple Access - synchronized slots';
        if (modeStatus) {
            modeStatus.innerHTML = '<span class="badge badge-success">Active</span>';
        }
        if (tdmaStatusText) tdmaStatusText.textContent = `Enabled (Stratum ${macState.stratum})`;

        // Calculate assigned slot based on node address
        const nodeAddr = state.nodeAddress;
        if (nodeAddr && assignedSlotText) {
            const addrNum = parseInt(nodeAddr.replace('0x', ''), 16);
            const slot = (addrNum % 9) + 1;  // Slots 1-9 (0 is beacon)
            assignedSlotText.textContent = `Slot ${slot}`;
        }
    } else {
        // CSMA-CA fallback
        if (modeIcon) modeIcon.textContent = '📡';
        if (modeName) modeName.textContent = 'CSMA-CA';
        if (modeDesc) modeDesc.textContent = 'Carrier-sense multiple access with collision avoidance';
        if (modeStatus) {
            modeStatus.innerHTML = '<span class="badge badge-warning">Fallback Mode</span>';
        }
        if (tdmaStatusText) tdmaStatusText.textContent = 'Disabled (no time sync)';
        if (assignedSlotText) assignedSlotText.textContent = 'None (CSMA mode)';
    }

    // Time quality text
    if (timeQualityText) {
        switch (macState.timeSource.toUpperCase()) {
            case 'GPS':
                timeQualityText.textContent = `GPS (Stratum ${macState.stratum})`;
                break;
            case 'NTP':
                timeQualityText.textContent = `NTP (Stratum ${macState.stratum})`;
                break;
            case 'SERIAL':
                timeQualityText.textContent = `Serial host (Stratum ${macState.stratum})`;
                break;
            case 'SYNCED':
                timeQualityText.textContent = `Network synced (Stratum ${macState.stratum})`;
                break;
            default:
                timeQualityText.textContent = 'Crystal only (no sync)';
        }
    }
}

// Parse radio status output from serial
function parseRadioStatusLine(line) {
    let match;

    // Frequency: 915.0 MHz
    match = line.match(/Frequency:\s*([\d.]+)/);
    if (match) {
        radioState.frequency = parseFloat(match[1]);
    }

    // TX Power: 22 dBm
    match = line.match(/TX Power:\s*(\d+)/);
    if (match) {
        radioState.txPower = parseInt(match[1]);
    }

    // SF: 10 or Spreading Factor: 10
    match = line.match(/(?:SF|Spreading Factor):\s*(\d+)/);
    if (match) {
        radioState.spreadingFactor = parseInt(match[1]);
    }

    // Channel: 0
    match = line.match(/Channel:\s*(\d+)/);
    if (match) {
        radioState.channel = parseInt(match[1]);
    }

    // RSSI: -65 dBm
    match = line.match(/RSSI:\s*(-?\d+)/);
    if (match) {
        radioState.rssi = parseInt(match[1]);
    }

    // SNR: 9 dB
    match = line.match(/SNR:\s*(-?\d+)/);
    if (match) {
        radioState.snr = parseInt(match[1]);
    }

    updateRadioDisplay();
}

// Export for debugging
window.meshState = state;
window.bleState = bleState;
window.sendCommand = sendCommand;
window.setConsoleFilter = setConsoleFilter;
window.scrollConsoleToBottom = scrollConsoleToBottom;
window.establishLinkFromUI = establishLinkFromUI;
window.showChannelTab = showChannelTab;
window.createChannel = createChannel;
window.joinChannel = joinChannel;
// BLE exports
window.connectBLE = connectBLE;
window.disconnectBLE = disconnectBLE;
window.sendBLECommand = sendBLECommand;
window.scanBLEPeers = scanBLEPeers;
window.connectToBLEPeer = connectToBLEPeer;
window.isBLEAvailable = isBLEAvailable;
// BLE Relay exports
window.enableBLERelayMode = enableBLERelayMode;
window.disableBLERelayMode = disableBLERelayMode;
window.sendRelayMessage = sendRelayMessage;
window.toggleRelayMode = toggleRelayMode;
window.bleState = bleState;
// LAN discovery exports
window.lanState = lanState;
window.startLANDiscovery = startLANDiscovery;
window.stopLANDiscovery = stopLANDiscovery;
window.sendLANMessage = sendLANMessage;
window.broadcastLANMessage = broadcastLANMessage;
window.sendMessageToLANPeer = sendMessageToLANPeer;
// WAN bridge exports
window.wanState = wanState;
window.connectWANBridge = connectWANBridge;
window.disconnectWANBridge = disconnectWANBridge;
window.sendWANMessage = sendWANMessage;
window.sendMessageToWANSite = sendMessageToWANSite;
// TDMA/MAC exports
window.macState = macState;
window.setTimeFromHost = setTimeFromHost;
window.updateTDMADisplay = updateTDMADisplay;
window.parseMACStatusLine = parseMACStatusLine;
// ARP exports
window.updateARPTable = updateARPTable;
// Radio exports
window.radioState = radioState;
window.updateRadioDisplay = updateRadioDisplay;
window.updateMACModeDisplay = updateMACModeDisplay;
window.parseRadioStatusLine = parseRadioStatusLine;
