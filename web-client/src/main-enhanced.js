/**
 * LNK-22 Enhanced Web Client
 * Full-featured mesh network monitoring and control interface
 */

// =============================================================================
// Global State Management
// =============================================================================

const state = {
    port: null,
    reader: null,
    writer: null,
    connected: false,

    // Network data
    nodeAddress: null,
    neighbors: new Map(),
    routes: new Map(),
    messages: [],

    // Statistics
    stats: {
        packetsReceived: 0,
        packetsSent: 0,
        beaconsSent: 0,
        routeRequests: 0,
        forwardedPackets: 0,
        uptime: 0,
        rssi: 0,
        snr: 0
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

    // History for charts
    history: {
        packets: [],
        rssi: [],
        snr: [],
        timestamps: []
    },

    // Console history
    consoleHistory: [],
    historyIndex: -1
};

// =============================================================================
// Serial Port Management
// =============================================================================

async function connectSerial() {
    try {
        // Request serial port
        state.port = await navigator.serial.requestPort();
        await state.port.open({ baudRate: 115200 });

        // Get reader and writer
        state.reader = state.port.readable.getReader();
        state.writer = state.port.writable.getWriter();
        state.connected = true;

        // Update UI
        updateConnectionStatus(true);
        addConsoleMessage('Connected to radio', 'success');

        // Request initial status
        setTimeout(() => {
            sendCommand('status');
            sendCommand('neighbors');
            sendCommand('routes');
            sendCommand('radio');
        }, 500);

        // Start reading
        readSerialData();

    } catch (error) {
        console.error('Connection error:', error);
        addConsoleMessage(`Connection failed: ${error.message}`, 'error');
        showToast('Failed to connect to radio', 'error');
    }
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
                addConsoleMessage('Serial stream ended', 'info');
                break;
            }

            buffer += decoder.decode(value, { stream: true });

            // Process complete lines
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

        // Provide more specific error messages
        if (error.message.includes('device has been lost')) {
            addConsoleMessage('Radio disconnected - please check USB connection', 'error');
            showToast('Radio disconnected', 'error');
        } else if (error.message.includes('break')) {
            addConsoleMessage('Serial break detected - radio may have reset', 'error');
            showToast('Radio reset detected', 'warning');
        } else {
            addConsoleMessage(`Read error: ${error.message}`, 'error');
            showToast(`Serial error: ${error.message}`, 'error');
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
// Serial Data Parser
// =============================================================================

function processSerialLine(line) {
    // Add to console
    addConsoleMessage(line, 'output');

    // Parse different message types
    if (line.startsWith('[STATUS]')) {
        parseStatusMessage(line);
    } else if (line.startsWith('[MESH]')) {
        parseMeshMessage(line);
    } else if (line.startsWith('[RADIO]')) {
        parseRadioMessage(line);
    } else if (line.startsWith('[GPS]')) {
        parseGPSMessage(line);
    } else if (line.includes('Address:')) {
        const match = line.match(/Address:\s*0x([0-9A-Fa-f]+)/);
        if (match) {
            state.nodeAddress = '0x' + match[1].toUpperCase();
            updateDashboard();
        }
    } else if (line.trim().match(/^0x[0-9A-Fa-f]+\s+0x[0-9A-Fa-f]+\s+/)) {
        // Route table row: starts with 0xDEST  0xNEXTHOP
        parseRouteLine(line);
    } else if (line.trim().match(/^0x[0-9A-Fa-f]+\s+/)) {
        // Neighbor table row: starts with 0xADDRESS followed by numbers
        parseNeighborLine(line);
    } else if (line.includes('Uptime:')) {
        const match = line.match(/Uptime:\s*(\d+)/);
        if (match) {
            state.stats.uptime = parseInt(match[1]);
            updateDashboard();
        }
    }
}

function parseStatusMessage(line) {
    // Parse status information
    const patterns = {
        packets_rx: /Packets RX:\s*(\d+)/,
        packets_tx: /Packets TX:\s*(\d+)/,
        beacons: /Beacons:\s*(\d+)/,
        routes: /Routes:\s*(\d+)/,
        neighbors: /Neighbors:\s*(\d+)/
    };

    for (const [key, pattern] of Object.entries(patterns)) {
        const match = line.match(pattern);
        if (match) {
            const value = parseInt(match[1]);
            switch (key) {
                case 'packets_rx':
                    state.stats.packetsReceived = value;
                    break;
                case 'packets_tx':
                    state.stats.packetsSent = value;
                    break;
                case 'beacons':
                    state.stats.beaconsSent = value;
                    break;
            }
        }
    }

    updateDashboard();
}

function parseMeshMessage(line) {
    // Count different packet types
    if (line.includes('ROUTE_REQ')) {
        state.stats.routeRequests++;
    } else if (line.includes('Forwarded packet')) {
        state.stats.forwardedPackets++;
    } else if (line.includes('New neighbor')) {
        const match = line.match(/neighbor:\s*0x([0-9A-Fa-f]+)/);
        if (match) {
            showToast(`New neighbor discovered: 0x${match[1]}`, 'success');
        }
    } else if (line.includes('Received packet')) {
        state.stats.packetsReceived++;
        updateDashboard();
    }
}

function parseRadioMessage(line) {
    // Parse RSSI and SNR
    const rssiMatch = line.match(/RSSI[:\s]+(-?\d+)/i);
    const snrMatch = line.match(/SNR[:\s]+(-?\d+)/i);

    if (rssiMatch) {
        state.stats.rssi = parseInt(rssiMatch[1]);
        updateSignalQuality();
    }
    if (snrMatch) {
        state.stats.snr = parseInt(snrMatch[1]);
        updateSignalQuality();
    }
}

function parseGPSMessage(line) {
    // Parse GPS data
    const latMatch = line.match(/Lat:\s*(-?\d+\.\d+)/);
    const lonMatch = line.match(/Lon:\s*(-?\d+\.\d+)/);
    const altMatch = line.match(/Alt:\s*(-?\d+\.\d+)/);
    const satMatch = line.match(/Sats:\s*(\d+)/);

    if (latMatch) state.gps.latitude = parseFloat(latMatch[1]);
    if (lonMatch) state.gps.longitude = parseFloat(lonMatch[1]);
    if (altMatch) state.gps.altitude = parseFloat(altMatch[1]);
    if (satMatch) state.gps.satellites = parseInt(satMatch[1]);

    if (state.gps.satellites > 0) {
        state.gps.fix = true;
        updateGPSMap();
    }
}

function parseNeighborLine(line) {
    // Parse neighbor information from table format
    // Format: 0x6FB69495  -42  8  1  2
    // (Address, RSSI, SNR, Packets, Age)

    // Skip header lines
    if (line.includes('===') || line.includes('Address') || line.includes('---')) {
        return;
    }

    // Match the table format: 0xADDRESS  RSSI  SNR  PKTS  AGE
    const match = line.match(/0x([0-9A-Fa-f]+)\s+(-?\d+)\s+(-?\d+)\s+(\d+)\s+(\d+)/);

    if (match) {
        const addr = '0x' + match[1].toUpperCase();
        const rssi = parseInt(match[2]);
        const snr = parseInt(match[3]);
        const packets = parseInt(match[4]);
        const age = parseInt(match[5]);

        state.neighbors.set(addr, {
            address: addr,
            rssi: rssi,
            snr: snr,
            packets: packets,
            age: age,
            lastSeen: Date.now()
        });

        updateNeighborGrid();
        updateNetworkGraph();

        console.log(`Parsed neighbor: ${addr}, RSSI=${rssi}, SNR=${snr}, Age=${age}s`);
    }
}

function parseRouteLine(line) {
    // Parse routing information from table format
    // Format: 0xDESTINATION  0xNEXTHOP  HOPS     QUALITY      AGE
    // Example: 0x1CE25673  0x9B69311E  2     75      10

    // Skip header lines
    if (line.includes('===') || line.includes('Destination') || line.includes('---')) {
        return;
    }

    // Match the table format: 0xDEST  0xNEXTHOP  HOPS  QUALITY  AGE
    const match = line.match(/0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+(\d+)\s+(\d+)\s+(\d+)/);

    if (match) {
        const dest = '0x' + match[1].toUpperCase();
        const nextHop = '0x' + match[2].toUpperCase();
        const hops = parseInt(match[3]);
        const quality = parseInt(match[4]);
        const age = parseInt(match[5]);

        state.routes.set(dest, {
            destination: dest,
            nextHop: nextHop,
            hops: hops,
            quality: quality,
            age: age
        });

        updateRoutingTable();
        updateNetworkGraph();

        console.log(`Parsed route: ${dest} via ${nextHop}, ${hops} hops, quality=${quality}%`);
    }
}

// =============================================================================
// UI Update Functions
// =============================================================================

function updateConnectionStatus(connected) {
    const statusDot = document.getElementById('statusIndicator');
    const statusText = document.getElementById('statusText');
    const statusBadge = document.getElementById('deviceStatus');
    const connectBtn = document.getElementById('connectBtn');
    const disconnectBtn = document.getElementById('disconnectBtn');

    if (connected) {
        if (statusDot) statusDot.className = 'status-dot connected';
        if (statusText) statusText.textContent = 'Connected';
        if (statusBadge) {
            statusBadge.className = 'badge badge-success';
            statusBadge.textContent = 'Online';
        }
        if (connectBtn) connectBtn.disabled = true;
        if (disconnectBtn) disconnectBtn.disabled = false;
    } else {
        if (statusDot) statusDot.className = 'status-dot disconnected';
        if (statusText) statusText.textContent = 'Disconnected';
        if (statusBadge) {
            statusBadge.className = 'badge badge-gray';
            statusBadge.textContent = 'Offline';
        }
        if (connectBtn) connectBtn.disabled = false;
        if (disconnectBtn) disconnectBtn.disabled = true;
    }
}

function updateDashboard() {
    // Update stat cards
    const totalPackets = state.stats.packetsReceived + state.stats.packetsSent;
    if (document.getElementById('totalPackets')) {
        document.getElementById('totalPackets').textContent = totalPackets;
    }
    if (document.getElementById('activeNeighbors')) {
        document.getElementById('activeNeighbors').textContent = state.neighbors.size;
    }

    // Update sidebar device card
    if (document.getElementById('deviceAddress')) {
        document.getElementById('deviceAddress').textContent = state.nodeAddress || '-';
    }
    if (document.getElementById('deviceUptime')) {
        document.getElementById('deviceUptime').textContent = formatUptime(state.stats.uptime);
    }
    if (document.getElementById('deviceRSSI')) {
        document.getElementById('deviceRSSI').textContent = `${state.stats.rssi} dBm`;
    }
    if (document.getElementById('deviceSNR')) {
        document.getElementById('deviceSNR').textContent = `${state.stats.snr} dB`;
    }
    if (document.getElementById('neighborCount')) {
        document.getElementById('neighborCount').textContent = state.neighbors.size;
    }
    if (document.getElementById('routeCount')) {
        document.getElementById('routeCount').textContent = state.routes.size;
    }

    // Update detailed stats
    if (document.getElementById('packetsReceived')) {
        document.getElementById('packetsReceived').textContent = state.stats.packetsReceived;
    }
    if (document.getElementById('packetsSent')) {
        document.getElementById('packetsSent').textContent = state.stats.packetsSent;
    }

    // Update last update time
    const lastUpdate = document.getElementById('lastUpdate');
    if (lastUpdate) {
        lastUpdate.textContent = `Last updated: ${new Date().toLocaleTimeString()}`;
    }

    // Update neighbor grid
    updateNeighborGrid();

    // Update charts
    updatePacketActivityChart();
}

function updateSignalQuality() {
    document.getElementById('rssiValue').textContent = `${state.stats.rssi} dBm`;
    document.getElementById('snrValue').textContent = `${state.stats.snr} dB`;

    // Update signal quality indicator
    const quality = calculateSignalQuality(state.stats.rssi, state.stats.snr);
    const indicator = document.getElementById('signalQualityIndicator');

    if (indicator) {
        indicator.className = 'signal-indicator signal-' + quality;
        indicator.textContent = quality.toUpperCase();
    }

    updateSignalQualityChart();
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
        const card = document.createElement('div');
        card.className = 'neighbor-card';
        card.innerHTML = `
            <div class="neighbor-header">
                <span class="neighbor-icon">📡</span>
                <code class="neighbor-addr">${neighbor.address}</code>
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
        `;
        grid.appendChild(card);
    }
}

function updateRoutingTable() {
    const tbody = document.getElementById('routesTableBody');
    if (!tbody) return;

    tbody.innerHTML = '';

    for (const [dest, route] of state.routes) {
        const row = document.createElement('tr');
        const path = buildPathVisualization(route);

        row.innerHTML = `
            <td><code>${route.destination}</code></td>
            <td><code>${route.nextHop}</code></td>
            <td>${route.hops}</td>
            <td>
                <div class="route-quality-bar">
                    <div class="route-quality-fill" style="width: ${route.quality}%"></div>
                </div>
                <span class="route-quality-text">${route.quality}%</span>
            </td>
            <td>${route.age}s</td>
            <td><div class="route-path">${path}</div></td>
        `;

        tbody.appendChild(row);
    }
}

function buildPathVisualization(route) {
    // Build visual path representation
    const nodes = [state.nodeAddress, route.nextHop];

    if (route.hops > 1) {
        for (let i = 1; i < route.hops - 1; i++) {
            nodes.push('...');
        }
    }

    nodes.push(route.destination);

    return nodes.map(n => `<span class="path-node">${n}</span>`).join(' → ');
}

function addConsoleMessage(message, type = 'output') {
    const consoleOutput = document.getElementById('console');
    if (!consoleOutput) return;

    const now = new Date();
    const timestamp = now.toLocaleTimeString();

    const line = document.createElement('div');
    line.className = `console-line console-${type}`;
    line.innerHTML = `
        <span class="timestamp">[${timestamp}]</span>
        <span class="message">${escapeHtml(message)}</span>
    `;

    consoleOutput.appendChild(line);
    consoleOutput.scrollTop = consoleOutput.scrollHeight;

    // Limit console to 500 lines
    while (consoleOutput.children.length > 500) {
        consoleOutput.removeChild(consoleOutput.firstChild);
    }
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function showToast(message, type = 'info') {
    const container = document.getElementById('toastContainer');
    if (!container) return;

    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.textContent = message;

    container.appendChild(toast);

    // Trigger animation
    setTimeout(() => toast.classList.add('show'), 10);

    // Remove after 3 seconds
    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => container.removeChild(toast), 300);
    }, 3000);
}

// =============================================================================
// Network Graph Visualization (D3.js)
// =============================================================================

let graphSimulation = null;

function initNetworkGraph() {
    const svg = d3.select('#networkGraph');
    const width = svg.node().parentElement.clientWidth;
    const height = 500;

    svg.attr('width', width).attr('height', height);

    // Create force simulation
    graphSimulation = d3.forceSimulation()
        .force('link', d3.forceLink().id(d => d.id).distance(150))
        .force('charge', d3.forceManyBody().strength(-400))
        .force('center', d3.forceCenter(width / 2, height / 2))
        .force('collision', d3.forceCollide().radius(40));

    updateNetworkGraph();
}

function updateNetworkGraph() {
    const svg = d3.select('#networkGraph');
    svg.selectAll('*').remove();

    if (!state.nodeAddress) return;

    // Build nodes and links
    const nodes = [{ id: state.nodeAddress, type: 'self' }];
    const links = [];

    // Add neighbors
    for (const [addr, neighbor] of state.neighbors) {
        nodes.push({ id: addr, type: 'neighbor', data: neighbor });
        links.push({ source: state.nodeAddress, target: addr, type: 'neighbor' });
    }

    // Add routed nodes
    for (const [dest, route] of state.routes) {
        if (!nodes.find(n => n.id === dest)) {
            nodes.push({ id: dest, type: 'remote', data: route });
        }

        // Add link to next hop if it exists
        if (nodes.find(n => n.id === route.nextHop)) {
            links.push({ source: route.nextHop, target: dest, type: 'route' });
        }
    }

    // Create arrow markers
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

    // Create links
    const link = svg.append('g')
        .selectAll('line')
        .data(links)
        .enter().append('line')
        .attr('class', d => `graph-link link-${d.type}`)
        .attr('marker-end', d => `url(#arrow-${d.type})`);

    // Create nodes
    const node = svg.append('g')
        .selectAll('g')
        .data(nodes)
        .enter().append('g')
        .attr('class', 'graph-node')
        .call(d3.drag()
            .on('start', dragStarted)
            .on('drag', dragged)
            .on('end', dragEnded));

    // Node circles
    node.append('circle')
        .attr('r', 20)
        .attr('class', d => `node-${d.type}`);

    // Node labels
    node.append('text')
        .text(d => d.id.substring(0, 8))
        .attr('text-anchor', 'middle')
        .attr('dy', 35)
        .attr('class', 'node-label');

    // Node icons
    node.append('text')
        .text(d => d.type === 'self' ? '📡' : d.type === 'neighbor' ? '📶' : '🔀')
        .attr('text-anchor', 'middle')
        .attr('dy', 5)
        .style('font-size', '20px');

    // Tooltips
    node.append('title')
        .text(d => {
            if (d.type === 'self') return `This Node\n${d.id}`;
            if (d.type === 'neighbor') {
                return `Neighbor\n${d.id}\nRSSI: ${d.data.rssi} dBm\nSNR: ${d.data.snr} dB`;
            }
            return `Remote Node\n${d.id}\nVia: ${d.data.nextHop}\nHops: ${d.data.hops}`;
        });

    // Update simulation
    if (graphSimulation) {
        graphSimulation.nodes(nodes);
        graphSimulation.force('link').links(links);
        graphSimulation.alpha(1).restart();

        graphSimulation.on('tick', () => {
            link
                .attr('x1', d => d.source.x)
                .attr('y1', d => d.source.y)
                .attr('x2', d => d.target.x)
                .attr('y2', d => d.target.y);

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
    // Packet Activity Chart
    const packetCtx = document.getElementById('packetChart');
    if (packetCtx) {
        state.charts.packetActivity = new Chart(packetCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: 'Packets Received',
                        data: [],
                        borderColor: '#4CAF50',
                        backgroundColor: 'rgba(76, 175, 80, 0.1)',
                        tension: 0.4
                    },
                    {
                        label: 'Packets Sent',
                        data: [],
                        borderColor: '#2196F3',
                        backgroundColor: 'rgba(33, 150, 243, 0.1)',
                        tension: 0.4
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: true,
                        position: 'top'
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true
                    }
                }
            }
        });
    }

    // Signal Quality Chart
    const signalCtx = document.getElementById('signalChart');
    if (signalCtx) {
        state.charts.signalQuality = new Chart(signalCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: 'RSSI (dBm)',
                        data: [],
                        borderColor: '#FF9800',
                        backgroundColor: 'rgba(255, 152, 0, 0.1)',
                        yAxisID: 'y',
                        tension: 0.4
                    },
                    {
                        label: 'SNR (dB)',
                        data: [],
                        borderColor: '#9C27B0',
                        backgroundColor: 'rgba(156, 39, 176, 0.1)',
                        yAxisID: 'y1',
                        tension: 0.4
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: true,
                        position: 'top'
                    }
                },
                scales: {
                    y: {
                        type: 'linear',
                        display: true,
                        position: 'left',
                        title: {
                            display: true,
                            text: 'RSSI (dBm)'
                        }
                    },
                    y1: {
                        type: 'linear',
                        display: true,
                        position: 'right',
                        title: {
                            display: true,
                            text: 'SNR (dB)'
                        },
                        grid: {
                            drawOnChartArea: false
                        }
                    }
                }
            }
        });
    }
}

function updatePacketActivityChart() {
    if (!state.charts.packetActivity) return;

    const now = new Date().toLocaleTimeString();
    const chart = state.charts.packetActivity;

    // Add data point
    chart.data.labels.push(now);
    chart.data.datasets[0].data.push(state.stats.packetsReceived);
    chart.data.datasets[1].data.push(state.stats.packetsSent);

    // Limit to 20 points
    if (chart.data.labels.length > 20) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
        chart.data.datasets[1].data.shift();
    }

    chart.update();
}

function updateSignalQualityChart() {
    if (!state.charts.signalQuality) return;

    const now = new Date().toLocaleTimeString();
    const chart = state.charts.signalQuality;

    // Add data point
    chart.data.labels.push(now);
    chart.data.datasets[0].data.push(state.stats.rssi);
    chart.data.datasets[1].data.push(state.stats.snr);

    // Limit to 20 points
    if (chart.data.labels.length > 20) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
        chart.data.datasets[1].data.shift();
    }

    chart.update();
}

// =============================================================================
// GPS Map (Leaflet)
// =============================================================================

let gpsMap = null;
let gpsMarker = null;

function initGPSMap() {
    const mapContainer = document.getElementById('map');
    if (!mapContainer) return;

    // Initialize map centered on US
    gpsMap = L.map('map').setView([39.8283, -98.5795], 4);

    // Add OpenStreetMap tiles
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '© OpenStreetMap contributors',
        maxZoom: 19
    }).addTo(gpsMap);
}

function updateGPSMap() {
    if (!gpsMap || !state.gps.fix) return;

    const lat = state.gps.latitude;
    const lon = state.gps.longitude;

    if (!lat || !lon) return;

    // Update or create marker
    if (gpsMarker) {
        gpsMarker.setLatLng([lat, lon]);
    } else {
        gpsMarker = L.marker([lat, lon]).addTo(gpsMap);
        gpsMarker.bindPopup(`
            <b>Node Position</b><br>
            Lat: ${lat.toFixed(6)}<br>
            Lon: ${lon.toFixed(6)}<br>
            Alt: ${state.gps.altitude}m<br>
            Sats: ${state.gps.satellites}
        `);
    }

    // Center map on marker
    gpsMap.setView([lat, lon], 15);

    // Update map nodes list
    const mapNodesList = document.getElementById('mapNodesList');
    if (mapNodesList) {
        mapNodesList.innerHTML = `
            <div class="map-node-item">
                <strong>${state.nodeAddress || 'This Node'}</strong><br>
                <small>Lat: ${lat.toFixed(6)}, Lon: ${lon.toFixed(6)}</small><br>
                <small>Alt: ${state.gps.altitude}m, Sats: ${state.gps.satellites}</small>
            </div>
        `;
    }
}

// =============================================================================
// Helper Functions
// =============================================================================

function calculateSignalQuality(rssi, snr) {
    // Classify signal quality based on RSSI and SNR
    if (rssi > -70 && snr > 5) return 'excellent';
    if (rssi > -90 && snr > 0) return 'good';
    if (rssi > -110 && snr > -5) return 'fair';
    return 'poor';
}

function calculateRouteQuality(hops) {
    // Simple quality metric based on hop count
    return Math.max(0, 100 - (hops - 1) * 25);
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

// =============================================================================
// Event Handlers
// =============================================================================

document.addEventListener('DOMContentLoaded', () => {
    // Connection buttons
    document.getElementById('connectBtn')?.addEventListener('click', connectSerial);

    // Tab navigation
    document.querySelectorAll('.nav-item').forEach(item => {
        item.addEventListener('click', (e) => {
            e.preventDefault();
            const tabId = item.getAttribute('data-tab') + 'Tab';
            switchTab(tabId);
        });
    });

    // Console input
    const consoleInput = document.getElementById('consoleInput');
    consoleInput?.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            const command = consoleInput.value.trim();
            if (command) {
                sendCommand(command);
                state.consoleHistory.push(command);
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

    // Console command shortcuts
    document.querySelectorAll('.console-shortcut').forEach(btn => {
        btn.addEventListener('click', () => {
            const command = btn.getAttribute('data-command');
            sendCommand(command);
        });
    });

    // Clear console button
    document.getElementById('clearConsole')?.addEventListener('click', () => {
        const consoleOutput = document.getElementById('consoleOutput');
        if (consoleOutput) consoleOutput.innerHTML = '';
    });

    // Message send button
    document.getElementById('sendBtn')?.addEventListener('click', sendMessage);
    document.getElementById('messageText')?.addEventListener('input', updateCharCount);
    document.getElementById('messageText')?.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && e.ctrlKey) {
            e.preventDefault();
            sendMessage();
        }
    });

    // Send command button
    document.getElementById('sendCmdBtn')?.addEventListener('click', () => {
        const input = document.getElementById('consoleInput');
        if (input && input.value.trim()) {
            sendCommand(input.value.trim());
            input.value = '';
        }
    });

    // Refresh buttons
    document.getElementById('refreshBtn')?.addEventListener('click', () => {
        sendCommand('status');
        sendCommand('neighbors');
        sendCommand('routes');
        showToast('Refreshing data...', 'info');
    });

    document.getElementById('refreshRoutes')?.addEventListener('click', () => {
        sendCommand('routes');
    });

    // Clear messages
    document.getElementById('clearMessages')?.addEventListener('click', () => {
        const messageList = document.getElementById('messagesList');
        if (messageList) {
            messageList.innerHTML = `
                <div class="empty-state">
                    <div class="empty-icon">💬</div>
                    <p>No messages yet</p>
                    <p class="text-muted">Send or receive messages via the mesh network</p>
                </div>
            `;
        }
    });

    // Initialize visualizations
    initNetworkGraph();
    initCharts();
    initGPSMap();

    // Auto-refresh data every 5 seconds
    setInterval(() => {
        if (state.connected) {
            sendCommand('status');
            sendCommand('neighbors');
        }
    }, 5000);

    // Update charts every 2 seconds
    setInterval(() => {
        if (state.connected) {
            updateDashboard();
        }
    }, 2000);
});

function switchTab(tabId) {
    // Hide all tabs
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });

    // Show selected tab
    const targetTab = document.getElementById(tabId);
    if (targetTab) {
        targetTab.classList.add('active');
    }

    // Update nav items
    document.querySelectorAll('.nav-item').forEach(item => {
        item.classList.remove('active');
        const itemTab = item.getAttribute('data-tab') + 'Tab';
        if (itemTab === tabId) {
            item.classList.add('active');
        }
    });

    // Special handling for map tab
    if (tabId === 'mapTab' && gpsMap) {
        setTimeout(() => gpsMap.invalidateSize(), 100);
    }

    // Special handling for network graph tab
    if (tabId === 'networkTab' && graphSimulation) {
        setTimeout(() => updateNetworkGraph(), 100);
    }
}

function sendMessage() {
    const destInput = document.getElementById('destAddress');
    const msgInput = document.getElementById('messageText');

    const dest = destInput.value.trim();
    const msg = msgInput.value.trim();

    if (!dest || !msg) {
        showToast('Please enter both destination and message', 'error');
        return;
    }

    if (msg.length > 255) {
        showToast('Message too long (max 255 characters)', 'error');
        return;
    }

    const command = `send ${dest} ${msg}`;
    sendCommand(command);

    // Add to message list
    const messageList = document.getElementById('messagesList');
    if (messageList) {
        // Remove empty state if present
        const emptyState = messageList.querySelector('.empty-state');
        if (emptyState) {
            emptyState.remove();
        }

        const msgEl = document.createElement('div');
        msgEl.className = 'message-item message-sent';
        msgEl.innerHTML = `
            <div class="message-header">
                <span class="message-from">To: ${dest}</span>
                <span class="message-time">${new Date().toLocaleTimeString()}</span>
            </div>
            <div class="message-body">${escapeHtml(msg)}</div>
        `;
        messageList.appendChild(msgEl);
        messageList.scrollTop = messageList.scrollHeight;
    }

    // Clear input
    msgInput.value = '';
    updateCharCount();

    showToast('Message sent', 'success');
}

function updateCharCount() {
    const msgInput = document.getElementById('messageText');
    const charCount = document.getElementById('charCount');
    const sendBtn = document.getElementById('sendBtn');

    if (msgInput && charCount) {
        const length = msgInput.value.length;
        charCount.textContent = length;

        if (sendBtn) {
            sendBtn.disabled = !state.connected || length === 0 || length > 255;
        }
    }
}

// Export for debugging
window.meshState = state;
