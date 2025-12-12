/**
 * MeshNet Web Client
 * Serial communication with MeshNet devices
 */

import { initMap, addNodePosition } from './map.js';

let port = null;
let reader = null;
let writer = null;
let isConnected = false;

// DOM elements
const connectBtn = document.getElementById('connectBtn');
const statusIndicator = document.getElementById('statusIndicator');
const statusText = document.getElementById('statusText');
const sendBtn = document.getElementById('sendBtn');
const sendCmdBtn = document.getElementById('sendCmdBtn');
const destAddress = document.getElementById('destAddress');
const messageText = document.getElementById('messageText');
const messagesList = document.getElementById('messagesList');
const consoleDiv = document.getElementById('console');
const consoleInput = document.getElementById('consoleInput');

// Nav tabs
const navItems = document.querySelectorAll('.nav-item');
const tabContents = document.querySelectorAll('.tab-content');

// Event listeners
connectBtn.addEventListener('click', toggleConnection);
sendBtn.addEventListener('click', sendMessage);
sendCmdBtn.addEventListener('click', sendCommand);

messageText.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && e.ctrlKey) {
        sendMessage();
    }
});

consoleInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
        sendCommand();
    }
});

// Tab navigation
navItems.forEach(item => {
    item.addEventListener('click', () => {
        const tabName = item.dataset.tab;
        switchTab(tabName);
    });
});

// Check for Web Serial support
if (!navigator.serial) {
    addConsole('❌ Web Serial API not supported. Use Chrome, Edge, or Opera.', 'error');
    connectBtn.disabled = true;
}

async function toggleConnection() {
    if (isConnected) {
        await disconnect();
    } else {
        await connect();
    }
}

async function connect() {
    try {
        addConsole('Requesting serial port...');

        // Request port
        port = await navigator.serial.requestPort();

        // Open port
        await port.open({ baudRate: 115200 });

        isConnected = true;
        updateConnectionUI();

        addConsole('✅ Connected to device', 'success');

        // Start reading
        readLoop();

        // Request status
        setTimeout(() => {
            sendSerialCommand('status');
        }, 1000);

    } catch (error) {
        addConsole('❌ Connection failed: ' + error.message, 'error');
    }
}

async function disconnect() {
    try {
        if (reader) {
            await reader.cancel();
            reader.releaseLock();
        }

        if (port) {
            await port.close();
        }

        port = null;
        reader = null;
        writer = null;
        isConnected = false;

        updateConnectionUI();
        addConsole('Disconnected from device');

    } catch (error) {
        addConsole('❌ Disconnect error: ' + error.message, 'error');
    }
}

async function readLoop() {
    try {
        const decoder = new TextDecoder();
        reader = port.readable.getReader();

        let buffer = '';

        while (true) {
            const { value, done } = await reader.read();
            if (done) {
                reader.releaseLock();
                break;
            }

            buffer += decoder.decode(value);

            // Process complete lines
            let newlineIndex;
            while ((newlineIndex = buffer.indexOf('\n')) !== -1) {
                const line = buffer.substring(0, newlineIndex).trim();
                buffer = buffer.substring(newlineIndex + 1);

                if (line) {
                    processSerialLine(line);
                }
            }
        }
    } catch (error) {
        if (error.toString().includes('cancel')) {
            // Normal cancellation
            return;
        }
        addConsole('❌ Read error: ' + error.message, 'error');
    }
}

async function sendSerialCommand(cmd) {
    if (!isConnected || !port) {
        return;
    }

    try {
        const encoder = new TextEncoder();
        const writer = port.writable.getWriter();

        await writer.write(encoder.encode(cmd + '\n'));
        writer.releaseLock();

        addConsole('> ' + cmd, 'command');

    } catch (error) {
        addConsole('❌ Send error: ' + error.message, 'error');
    }
}

async function sendMessage() {
    const dest = destAddress.value.trim();
    const msg = messageText.value.trim();

    if (!dest || !msg) {
        addConsole('❌ Please enter destination and message', 'error');
        return;
    }

    const cmd = `send ${dest} ${msg}`;
    await sendSerialCommand(cmd);

    // Add to message list
    addMessage(msg, 'sent', dest);

    // Clear input
    messageText.value = '';
}

async function sendCommand() {
    const cmd = consoleInput.value.trim();
    if (!cmd) return;

    await sendSerialCommand(cmd);
    consoleInput.value = '';
}

// State machine for parsing multi-line MESSAGE blocks
let messageParseState = {
    inMessage: false,
    source: '',
    type: '',
    content: ''
};

// Track sent messages for delivery status updates
// Maps destination -> array of {id, timestamp, element}
let sentMessages = new Map();
let messageIdCounter = 0;

function processSerialLine(line) {
    addConsole(line);

    // Parse delivery status updates
    // Format: [DELIVERY] Packet 123 to 0xDAD930F0 status: ACKED
    if (line.includes('[DELIVERY]')) {
        const match = line.match(/\[DELIVERY\] Packet (\d+) to (0x[0-9A-Fa-f]+) status: (\w+)/);
        if (match) {
            const packetId = match[1];
            const destination = match[2];
            const status = match[3];
            updateDeliveryStatus(destination, status);
        }
        return;
    }

    // Parse multi-line MESSAGE blocks from firmware
    // Format:
    // ========================================
    // MESSAGE from 0xABCDEF12 (BROADCAST)
    // ----------------------------------------
    // Message content here
    // ========================================

    if (line.includes('MESSAGE from 0x')) {
        // Start of message block - extract source address and type
        // Formats:
        //   "MESSAGE from 0xABCD (BROADCAST)"
        //   "MESSAGE from 0xABCD (DIRECT)"
        //   "MESSAGE from 0xABCD (BLE) (BROADCAST)"
        //   "MESSAGE from 0xABCD (BLE) (DIRECT)"
        const match = line.match(/MESSAGE from (0x[0-9A-Fa-f]+).*\((BROADCAST|DIRECT)\)/);
        if (match) {
            messageParseState.inMessage = true;
            messageParseState.source = match[1];
            messageParseState.type = match[2];
            messageParseState.content = '';
        }
        return;
    }

    if (messageParseState.inMessage) {
        // Skip separator lines
        if (line.match(/^[-=]+$/)) {
            // End of message block (second ===== line)
            if (line.startsWith('===') && messageParseState.content) {
                const typeStr = messageParseState.type === 'BROADCAST' ? ' (broadcast)' : '';
                addMessage(messageParseState.content, 'received', messageParseState.source + typeStr);
                messageParseState.inMessage = false;
                messageParseState.content = '';
            }
            return;
        }

        // Accumulate message content
        if (messageParseState.content) {
            messageParseState.content += '\n' + line;
        } else {
            messageParseState.content = line;
        }
        return;
    }

    // Legacy: Parse old format if still used
    if (line.includes('Received message:')) {
        const msg = line.split('Received message:')[1].trim();
        addMessage(msg, 'received', 'Unknown');
    }
    else if (line.includes('Node Address:')) {
        const addr = line.split('Node Address:')[1].trim();
        document.getElementById('deviceAddress').textContent = addr;
    }
    else if (line.includes('Uptime:')) {
        const uptime = line.split('Uptime:')[1].trim();
        document.getElementById('deviceUptime').textContent = uptime;
    }
    else if (line.includes('Radio RSSI:')) {
        const rssi = line.split('Radio RSSI:')[1].trim();
        document.getElementById('deviceRSSI').textContent = rssi;
    }
    else if (line.includes('Neighbors:')) {
        const count = line.split('Neighbors:')[1].trim();
        document.getElementById('deviceNeighbors').textContent = count;
    }
    else if (line.includes('Packets Sent:')) {
        const count = line.split('Packets Sent:')[1].trim();
        document.getElementById('statPacketsSent').textContent = count;
    }
    else if (line.includes('Packets Received:')) {
        const count = line.split('Packets Received:')[1].trim();
        document.getElementById('statPacketsReceived').textContent = count;
    }
}

function addMessage(content, type, from) {
    // Remove empty state if present
    const emptyState = messagesList.querySelector('.empty-state');
    if (emptyState) {
        emptyState.remove();
    }
    const emptyMsgs = messagesList.querySelector('.chat-messages-empty');
    if (emptyMsgs) {
        emptyMsgs.remove();
    }

    const msgDiv = document.createElement('div');
    msgDiv.className = `message ${type}`;
    msgDiv.id = `msg-${++messageIdCounter}`;

    const now = new Date();
    const timeStr = now.toLocaleTimeString();

    // For sent messages, add delivery status indicator
    const statusIcon = type === 'sent' ? '<span class="delivery-status pending" title="Pending">⏳</span>' : '';

    msgDiv.innerHTML = `
        <div class="message-header">
            <span class="message-from">${type === 'sent' ? 'You → ' + from : 'From ' + from}</span>
            <span class="message-time">${timeStr}</span>
            ${statusIcon}
        </div>
        <div class="message-content">${content}</div>
    `;

    messagesList.appendChild(msgDiv);
    messagesList.scrollTop = messagesList.scrollHeight;

    // Track sent messages for delivery status updates
    if (type === 'sent') {
        // Normalize destination address (handle both "0xABC" and "broadcast")
        let destKey = from.toLowerCase();
        if (destKey.includes('broadcast') || destKey === '0xffffffff') {
            destKey = 'broadcast';
        }

        if (!sentMessages.has(destKey)) {
            sentMessages.set(destKey, []);
        }
        sentMessages.get(destKey).push({
            id: messageIdCounter,
            timestamp: Date.now(),
            element: msgDiv
        });

        // Keep only last 20 messages per destination
        const msgs = sentMessages.get(destKey);
        if (msgs.length > 20) {
            msgs.shift();
        }
    }

    return msgDiv;
}

// Update delivery status for messages sent to a destination
function updateDeliveryStatus(destination, status) {
    // Normalize destination
    let destKey = destination.toLowerCase();
    if (destKey === '0xffffffff') {
        destKey = 'broadcast';
    }

    // Status icons and classes
    const statusInfo = {
        'PENDING': { icon: '⏳', class: 'pending', title: 'Pending' },
        'SENT': { icon: '✓', class: 'sent', title: 'Sent' },
        'ACKED': { icon: '✓✓', class: 'acked', title: 'Delivered' },
        'FAILED': { icon: '✗', class: 'failed', title: 'Failed' },
        'NO_ROUTE': { icon: '⚠', class: 'no-route', title: 'No Route' }
    };

    const info = statusInfo[status] || statusInfo['PENDING'];

    // Find the most recent pending message to this destination
    const msgs = sentMessages.get(destKey);
    if (msgs && msgs.length > 0) {
        // Update the most recent message that hasn't been finalized
        for (let i = msgs.length - 1; i >= 0; i--) {
            const msg = msgs[i];
            const statusEl = msg.element.querySelector('.delivery-status');
            if (statusEl && (statusEl.classList.contains('pending') || statusEl.classList.contains('sent'))) {
                statusEl.textContent = info.icon;
                statusEl.className = `delivery-status ${info.class}`;
                statusEl.title = info.title;

                // If this is a final status (ACKED, FAILED, NO_ROUTE), stop looking
                if (status === 'ACKED' || status === 'FAILED' || status === 'NO_ROUTE') {
                    break;
                }
                break;
            }
        }
    }
}

function addConsole(text, type = '') {
    const line = document.createElement('div');
    line.className = 'console-line ' + type;

    const timestamp = new Date().toLocaleTimeString();
    line.textContent = `[${timestamp}] ${text}`;

    consoleDiv.appendChild(line);
    consoleDiv.scrollTop = consoleDiv.scrollHeight;
}

function updateConnectionUI() {
    if (isConnected) {
        statusIndicator.className = 'status-dot connected';
        statusText.textContent = 'Connected';
        connectBtn.textContent = 'Disconnect';
        sendBtn.disabled = false;
        sendCmdBtn.disabled = false;
    } else {
        statusIndicator.className = 'status-dot disconnected';
        statusText.textContent = 'Disconnected';
        connectBtn.textContent = 'Connect';
        sendBtn.disabled = true;
        sendCmdBtn.disabled = true;
        document.getElementById('deviceAddress').textContent = '-';
        document.getElementById('deviceUptime').textContent = '-';
        document.getElementById('deviceRSSI').textContent = '-';
        document.getElementById('deviceNeighbors').textContent = '0';
    }
}

function switchTab(tabName) {
    // Update nav items
    navItems.forEach(item => {
        if (item.dataset.tab === tabName) {
            item.classList.add('active');
        } else {
            item.classList.remove('active');
        }
    });

    // Update tab contents
    tabContents.forEach(content => {
        if (content.id === tabName + 'Tab') {
            content.classList.add('active');
        } else {
            content.classList.remove('active');
        }
    });

    // Initialize map when switching to map tab
    if (tabName === 'map') {
        setTimeout(() => initMap(), 100);  // Delay to ensure DOM is ready
    }

    // Refresh data when switching tabs
    if (isConnected) {
        if (tabName === 'network') {
            sendSerialCommand('routes');
            sendSerialCommand('neighbors');
        } else if (tabName === 'status') {
            sendSerialCommand('status');
        }
    }
}

// Initialize
addConsole('MeshNet Web Client ready');
addConsole('Click "Connect" to connect to your device');
