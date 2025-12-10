/**
 * MeshNet Web Client - Map Functionality
 * GPS position visualization using Leaflet
 */

let map = null;
let markers = {};
const nodePositions = {};

export function initMap() {
    if (map) return;

    // Initialize map centered on world
    map = L.map('map').setView([37.0902, -95.7129], 4);

    // Add OpenStreetMap tiles
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '© OpenStreetMap contributors',
        maxZoom: 19
    }).addTo(map);

    console.log('Map initialized');
}

export function addNodePosition(address, lat, lon, alt, sats, fixType) {
    if (!map) {
        initMap();
    }

    const key = address.toString(16).padStart(8, '0');

    // Store position
    nodePositions[key] = {
        address,
        lat,
        lon,
        alt,
        sats,
        fixType,
        timestamp: new Date()
    };

    // Remove old marker if exists
    if (markers[key]) {
        map.removeLayer(markers[key]);
    }

    // Add new marker
    const marker = L.marker([lat, lon])
        .addTo(map)
        .bindPopup(`
            <b>Node 0x${key}</b><br>
            Lat: ${lat.toFixed(6)}<br>
            Lon: ${lon.toFixed(6)}<br>
            Alt: ${alt.toFixed(1)}m<br>
            Sats: ${sats}<br>
            Fix: ${fixType === 3 ? '3D' : fixType === 2 ? '2D' : 'No fix'}
        `);

    markers[key] = marker;

    // Center map on latest position
    map.setView([lat, lon], 13);

    // Update nodes list
    updateNodesList();
}

function updateNodesList() {
    const nodesList = document.getElementById('nodesList');
    if (!nodesList) return;

    const nodes = Object.keys(nodePositions);
    if (nodes.length === 0) {
        nodesList.innerHTML = '<div class="text-muted">No position data yet</div>';
        return;
    }

    nodesList.innerHTML = nodes.map(key => {
        const pos = nodePositions[key];
        const age = Math.floor((new Date() - pos.timestamp) / 1000);
        return `
            <div style="padding: 8px; border-bottom: 1px solid #e5e5e5; cursor: pointer;" onclick="map.setView([${pos.lat}, ${pos.lon}], 15)">
                <strong>0x${key}</strong><br>
                <small>${pos.lat.toFixed(6)}, ${pos.lon.toFixed(6)} (${pos.sats} sats) - ${age}s ago</small>
            </div>
        `;
    }).join('');
}

export function clearMap() {
    Object.values(markers).forEach(marker => map.removeLayer(marker));
    markers = {};
    nodePositions.clear();
    updateNodesList();
}
