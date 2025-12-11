/**
 * LNK-22 Enhanced Web Client v1.8.0
 * Full-featured mesh network monitoring and control interface
 * Supports all firmware features: Links, Groups, DTN, Emergency, ADR, Store-Forward
 */

// =============================================================================
// Version Information
// =============================================================================

const WEB_CLIENT_VERSION = '1.8.0';
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
            await disconnectSerial();
        }

        state.port = await navigator.serial.requestPort();
        await state.port.open({ baudRate: 115200 });

        state.reader = state.port.readable.getReader();
        state.writer = state.port.writable.getWriter();
        state.connected = true;

        updateConnectionStatus(true);
        addConsoleMessage('Connected to radio', 'success');
        showToast('Connected to radio', 'success');

        // Query ALL feature statuses on connect
        setTimeout(() => {
            queryAllStatus();
        }, 500);

        readSerialData();

    } catch (error) {
        console.error('Connection error:', error);
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

function queryAllStatus() {
    // Basic status
    sendCommand('status');

    // After a delay, query all features
    setTimeout(() => sendCommand('name'), 200);
    setTimeout(() => sendCommand('name list'), 400);
    setTimeout(() => sendCommand('neighbors'), 600);
    setTimeout(() => sendCommand('routes'), 800);
    setTimeout(() => sendCommand('radio'), 1000);

    // Advanced features
    setTimeout(() => sendCommand('link'), 1200);
    setTimeout(() => sendCommand('group'), 1400);
    setTimeout(() => sendCommand('queue'), 1600);
    setTimeout(() => sendCommand('adr'), 1800);
    setTimeout(() => sendCommand('history'), 2000);
}

async function disconnectSerial() {
    try {
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

    try {
        while (state.connected) {
            if (!state.reader) break;

            const { value, done } = await state.reader.read();
            if (done) {
                console.log('Serial read done signal received');
                break;
            }

            buffer += decoder.decode(value, { stream: true });

            let newlineIndex;
            while ((newlineIndex = buffer.indexOf('\n')) >= 0) {
                const line = buffer.slice(0, newlineIndex).trim();
                buffer = buffer.slice(newlineIndex + 1);

                if (line) {
                    processSerialLine(line);
                }
            }
        }
    } catch (error) {
        console.error('Read error:', error);
        const errorMsg = error.message || String(error);

        // Handle "device has been lost" gracefully
        if (errorMsg.includes('device has been lost') || errorMsg.includes('disconnected')) {
            addConsoleMessage('Device disconnected - please reconnect', 'warning');
            showToast('Device disconnected', 'warning');
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
// Serial Data Parser - Complete Implementation
// =============================================================================

function processSerialLine(line) {
    addConsoleMessage(line, 'output');

    // Track section headers for multi-line parsing
    if (line.startsWith('===')) {
        if (line.includes('LNK-22 Status')) state.currentSection = 'status';
        else if (line.includes('Link Status') || line.includes('Link ===')) state.currentSection = 'link';
        else if (line.includes('Group')) state.currentSection = 'group';
        else if (line.includes('Store-and-Forward')) state.currentSection = 'sf';
        else if (line.includes('Adaptive Data Rate')) state.currentSection = 'adr';
        else if (line.includes('Neighbor')) state.currentSection = 'neighbors';
        else if (line.includes('Route') || line.includes('Routing')) state.currentSection = 'routes';
        else if (line.includes('Emergency') || line.includes('SOS')) state.currentSection = 'sos';
        else state.currentSection = null;
        console.log(`Section detected: ${state.currentSection}`);
        return;
    }

    // End of section
    if (line.match(/^=+$/)) {
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
    if (line.includes('===') || line.includes('Address') || line.includes('---') || line.includes('(none)')) {
        return;
    }

    // Format: 0x867BDA10 RSSI:-64 SNR:9 (11 pkts, 27s ago)
    // Can have leading whitespace
    let match = line.match(/0x([0-9A-Fa-f]+)\s+RSSI:(-?\d+)\s+SNR:(-?\d+)\s+\((\d+)\s+pkts?,\s*(\d+)s/i);
    if (match) {
        const addr = '0x' + match[1].toUpperCase();
        state.neighbors.set(addr, {
            address: addr,
            name: state.nodeNames.get(addr) || null,
            rssi: parseInt(match[2]),
            snr: parseInt(match[3]),
            packets: parseInt(match[4]),
            age: parseInt(match[5]),
            lastSeen: Date.now()
        });
        console.log(`Parsed neighbor: ${addr} RSSI:${match[2]} SNR:${match[3]}`);
        updateNeighborGrid();
        return;
    }

    // Format with name: Alpha RSSI:-65 SNR:8 (2 pkts, 10s ago)
    match = line.match(/^\s*(\w+)\s+RSSI:(-?\d+)\s+SNR:(-?\d+)\s+\((\d+)\s+pkts?,\s*(\d+)s/);
    if (match) {
        const name = match[1];
        // Find address for this name or use name as key
        let addr = name;
        for (const [a, n] of state.nodeNames) {
            if (n === name) { addr = a; break; }
        }
        state.neighbors.set(addr, {
            address: addr,
            name: name,
            rssi: parseInt(match[2]),
            snr: parseInt(match[3]),
            packets: parseInt(match[4]),
            age: parseInt(match[5]),
            lastSeen: Date.now()
        });
        console.log(`Parsed neighbor by name: ${name} RSSI:${match[2]}`);
        updateNeighborGrid();
        return;
    }

    // Table format: 0xADDRESS  RSSI  SNR  PKTS  AGE (space separated)
    match = line.match(/0x([0-9A-Fa-f]+)\s+(-?\d+)\s+(-?\d+)\s+(\d+)\s+(\d+)/);
    if (match) {
        const addr = '0x' + match[1].toUpperCase();
        state.neighbors.set(addr, {
            address: addr,
            name: state.nodeNames.get(addr) || null,
            rssi: parseInt(match[2]),
            snr: parseInt(match[3]),
            packets: parseInt(match[4]),
            age: parseInt(match[5]),
            lastSeen: Date.now()
        });
        console.log(`Parsed neighbor table: ${addr}`);
        updateNeighborGrid();
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
    // Handle incoming mesh messages
    // Format: "MESSAGE from 0xABCD1234 (BROADCAST)" or "MESSAGE from 0xABCD1234 (DIRECT)"
    let match = line.match(/MESSAGE from 0x([0-9A-Fa-f]+)\s*\((\w+)\)/);
    if (match) {
        const fromAddr = '0x' + match[1].toUpperCase();
        const msgType = match[2];  // BROADCAST or DIRECT
        pendingMessage = {
            from: fromAddr,
            fromName: state.nodeNames.get(fromAddr) || fromAddr,
            type: msgType,
            content: '',
            timestamp: new Date()
        };
        messageContentStarted = false;
        console.log(`[MSG] Starting message capture from ${fromAddr}`);
        return;
    }

    // If we're capturing a message
    if (pendingMessage) {
        // Start capturing after the separator line (----)
        if (line.match(/^-+$/)) {
            messageContentStarted = true;
            return;
        }

        // End of message (====)
        if (line.match(/^=+$/)) {
            if (pendingMessage.content.trim()) {
                addReceivedMessage(pendingMessage);
                console.log(`[MSG] Message captured: "${pendingMessage.content}"`);
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
        }
        return;
    }

    // Node name: Alpha (0x4D77048F)
    match = line.match(/Node(?:\s+name)?:\s*(\w+)\s+\(0x([0-9A-Fa-f]+)\)/);
    if (match) {
        state.nodeName = match[1];
        state.nodeAddress = '0x' + match[2].toUpperCase();
        state.nodeNames.set(state.nodeAddress, state.nodeName);
        updateDashboard();
        return;
    }

    // Name list local: * Alpha (0x4D77048F) [local]
    match = line.match(/\*\s+(\w+)\s+\(0x([0-9A-Fa-f]+)\)\s+\[local\]/);
    if (match) {
        state.nodeName = match[1];
        state.nodeAddress = '0x' + match[2].toUpperCase();
        state.nodeNames.set(state.nodeAddress, state.nodeName);
        updateDashboard();
        return;
    }

    // Name list remote: Alpha (0xDAD930F0)
    match = line.match(/^\s+(\w+)\s+\(0x([0-9A-Fa-f]+)\)/);
    if (match && !line.includes('[local]')) {
        const addr = '0x' + match[2].toUpperCase();
        state.nodeNames.set(addr, match[1]);
        return;
    }

    // Added name 'X' for 0xADDRESS
    match = line.match(/Added name '(\w+)' for 0x([0-9A-Fa-f]+)/);
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
    // Node info
    const deviceAddr = document.getElementById('deviceAddress');
    if (deviceAddr) {
        deviceAddr.textContent = state.nodeName
            ? `${state.nodeName} (${state.nodeAddress})`
            : (state.nodeAddress || '-');
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
                    <code class="link-addr">${addr}</code>
                </div>
                <div class="link-meta">
                    <span class="badge ${fsClass}">${fsText}</span>
                    <span class="badge badge-gray">${link.state || 'ACTIVE'}</span>
                </div>
                <button class="btn btn-small btn-danger" onclick="closeLink('${addr}')">Close</button>
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
                <code class="known-node-addr">${addr}</code>
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
        const displayName = neighbor.name || state.nodeNames.get(addr) || addr;
        const card = document.createElement('div');
        card.className = 'neighbor-card';
        card.innerHTML = `
            <div class="neighbor-header">
                <span class="neighbor-icon">📡</span>
                <span class="neighbor-name">${escapeHtml(displayName)}</span>
            </div>
            <code class="neighbor-addr">${addr}</code>
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
                <button class="btn btn-small btn-primary" onclick="sendMessageTo('${addr}')">Message</button>
                <button class="btn btn-small btn-secondary" onclick="establishLink('${addr}')">Link</button>
            </div>
        `;
        grid.appendChild(card);
    }

    // Update quick destination buttons in Messages tab
    updateQuickDestinations();
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
            <td><strong>${escapeHtml(destName)}</strong><br><code>${route.destination}</code></td>
            <td>${escapeHtml(nextHopName)}<br><code>${route.nextHop}</code></td>
            <td>${route.hops}</td>
            <td>
                <div class="route-quality-bar"><div class="route-quality-fill" style="width: ${route.quality}%"></div></div>
                ${route.quality}%
            </td>
            <td>${route.age}s</td>
            <td><button class="btn btn-small btn-primary" onclick="sendMessageTo('${route.destination}')">Send</button></td>
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

    const nodes = [{ id: state.nodeAddress, type: 'self', name: state.nodeName }];
    const links = [];

    for (const [addr, neighbor] of state.neighbors) {
        nodes.push({ id: addr, type: 'neighbor', data: neighbor, name: neighbor.name || state.nodeNames.get(addr) });
        links.push({ source: state.nodeAddress, target: addr, type: 'neighbor', rssi: neighbor.rssi });
    }

    for (const [dest, route] of state.routes) {
        if (!nodes.find(n => n.id === dest)) {
            nodes.push({ id: dest, type: 'remote', data: route, name: state.nodeNames.get(dest) });
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
        .text(d => d.name || d.id.substring(2, 10))
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
            const name = d.name || d.id;
            if (d.type === 'self') return `This Node\n${name}`;
            if (d.type === 'neighbor') return `Neighbor: ${name}\nRSSI: ${d.data.rssi} dBm\nSNR: ${d.data.snr} dB`;
            return `Remote: ${name}\nHops: ${d.data.hops}`;
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

function getDisplayName(addr) {
    return state.nodeNames.get(addr) || addr;
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

function addConsoleMessage(message, type = 'output') {
    const consoleEl = document.getElementById('console');
    if (!consoleEl) return;

    const line = document.createElement('div');
    line.className = `console-line console-${type}`;
    line.innerHTML = `<span class="timestamp">[${new Date().toLocaleTimeString()}]</span> <span class="message">${escapeHtml(message)}</span>`;

    consoleEl.appendChild(line);
    consoleEl.scrollTop = consoleEl.scrollHeight;

    while (consoleEl.children.length > 500) {
        consoleEl.removeChild(consoleEl.firstChild);
    }
}

// =============================================================================
// Action Functions
// =============================================================================

function establishLink(addr) {
    const name = state.nodeNames.get(addr) || addr;
    sendCommand(`link ${name}`);
    showToast(`Requesting link to ${name}...`, 'info');
}

function closeLink(addr) {
    const name = state.nodeNames.get(addr) || addr;
    sendCommand(`link close ${name}`);
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

    const dest = destInput?.value.trim();
    const msg = msgInput?.value.trim();

    if (!dest || !msg) {
        showToast('Enter destination and message', 'error');
        return;
    }

    sendCommand(`send ${dest} ${msg}`);

    const messageList = document.getElementById('messagesList');
    if (messageList) {
        const emptyState = messageList.querySelector('.empty-state');
        if (emptyState) emptyState.remove();

        const msgEl = document.createElement('div');
        msgEl.className = 'message-item message-sent';
        msgEl.innerHTML = `
            <div class="message-header">
                <span class="message-from">To: ${escapeHtml(dest)}</span>
                <span class="message-time">${new Date().toLocaleTimeString()}</span>
            </div>
            <div class="message-body">${escapeHtml(msg)}</div>
        `;
        messageList.appendChild(msgEl);
        messageList.scrollTop = messageList.scrollHeight;
    }

    msgInput.value = '';
    showToast('Message sent', 'success');
}

/**
 * Add a received message to the messages list and state
 */
function addReceivedMessage(msg) {
    // Add to state
    state.messages.push(msg);

    // Update UI
    const messageList = document.getElementById('messagesList');
    if (!messageList) return;

    // Remove empty state if present
    const emptyState = messageList.querySelector('.empty-state');
    if (emptyState) emptyState.remove();

    // Create message element
    const msgEl = document.createElement('div');
    msgEl.className = `message-item message-received ${msg.type.toLowerCase()}`;

    const typeIcon = msg.type === 'BROADCAST' ? '📢' : '💬';
    const typeBadge = msg.type === 'BROADCAST'
        ? '<span class="msg-badge broadcast">Broadcast</span>'
        : '<span class="msg-badge direct">Direct</span>';

    msgEl.innerHTML = `
        <div class="message-header">
            <span class="message-from">${typeIcon} From: ${escapeHtml(msg.fromName)}</span>
            ${typeBadge}
            <span class="message-time">${msg.timestamp.toLocaleTimeString()}</span>
        </div>
        <div class="message-body">${escapeHtml(msg.content)}</div>
        <div class="message-footer">
            <span class="message-addr">${msg.from}</span>
        </div>
    `;

    messageList.appendChild(msgEl);
    messageList.scrollTop = messageList.scrollHeight;

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
 * Update quick destination buttons with current neighbors
 */
function updateQuickDestinations() {
    const container = document.getElementById('quickDestNeighbors');
    if (!container) return;

    container.innerHTML = '';

    // Add a button for each neighbor
    state.neighbors.forEach((neighbor, addr) => {
        const name = state.nodeNames.get(addr) || addr;
        const btn = document.createElement('button');
        btn.className = 'quick-dest-btn';
        btn.dataset.dest = addr;
        btn.title = `Send to ${name}`;
        btn.innerHTML = `📍 ${escapeHtml(name)}`;
        btn.addEventListener('click', () => {
            const destInput = document.getElementById('destAddress');
            if (destInput) {
                destInput.value = addr;
                document.querySelectorAll('.quick-dest-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
            }
        });
        container.appendChild(btn);
    });
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
        const messageList = document.getElementById('messagesList');
        if (messageList) {
            messageList.innerHTML = `
                <div class="empty-state">
                    <div class="empty-icon">💬</div>
                    <h3>No Messages Yet</h3>
                    <p class="text-muted">Messages you send and receive will appear here.</p>
                </div>
            `;
        }
        updateMessageCount();
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

    // Group creation
    document.getElementById('createGroupBtn')?.addEventListener('click', () => {
        const name = document.getElementById('newGroupName')?.value.trim();
        if (name) {
            sendCommand(`group create ${name}`);
            document.getElementById('newGroupName').value = '';
            setTimeout(() => sendCommand('group'), 500);
        }
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

// Export for debugging
window.meshState = state;
window.sendCommand = sendCommand;
