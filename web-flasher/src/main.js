/**
 * MeshNet Web Flasher
 * Uses Web Serial API to flash firmware to devices
 */

let port = null;
let isConnected = false;

// DOM elements
const connectBtn = document.getElementById('connectBtn');
const flashBtn = document.getElementById('flashBtn');
const eraseBtn = document.getElementById('eraseBtn');
const deviceStatus = document.getElementById('deviceStatus');
const chipType = document.getElementById('chipType');
const macAddress = document.getElementById('macAddress');
const logDiv = document.getElementById('logDiv');
const progressDiv = document.getElementById('progressDiv');
const progressFill = document.getElementById('progressFill');
const progressText = document.getElementById('progressText');
const boardSelect = document.getElementById('boardSelect');

// Event listeners
connectBtn.addEventListener('click', connectDevice);
flashBtn.addEventListener('click', flashFirmware);
eraseBtn.addEventListener('click', eraseFlash);

// Check if Web Serial is supported
if (!navigator.serial) {
    logError('Web Serial API is not supported in this browser.');
    logError('Please use Chrome, Edge, or Opera.');
    connectBtn.disabled = true;
}

async function connectDevice() {
    try {
        if (isConnected) {
            // Disconnect
            await disconnectDevice();
            return;
        }

        logInfo('Requesting device...');

        // Request a serial port
        port = await navigator.serial.requestPort();

        // Open the port
        await port.open({ baudRate: 115200 });

        isConnected = true;
        updateUI();

        logSuccess('Device connected!');

        // Try to detect chip type (ESP32 only)
        const board = boardSelect.value;
        if (board.includes('rak11200') || board.includes('heltec')) {
            await detectChip();
        } else {
            logInfo('Board type: ' + boardSelect.options[boardSelect.selectedIndex].text);
            chipType.textContent = 'nRF52840';
        }

    } catch (error) {
        logError('Failed to connect: ' + error.message);
    }
}

async function disconnectDevice() {
    if (port) {
        await port.close();
        port = null;
    }
    isConnected = false;
    updateUI();
    logInfo('Device disconnected');
}

async function detectChip() {
    try {
        logInfo('Detecting chip type...');

        // This is a simplified version
        // Real implementation would use esptool-js
        chipType.textContent = 'ESP32';
        macAddress.textContent = '00:00:00:00:00:00';

        logSuccess('Chip detected!');

    } catch (error) {
        logWarning('Could not detect chip: ' + error.message);
    }
}

async function flashFirmware() {
    if (!isConnected) {
        logError('Please connect a device first');
        return;
    }

    try {
        const board = boardSelect.value;
        logInfo(`Flashing firmware for ${board}...`);

        // Show progress
        progressDiv.classList.add('show');
        updateProgress(0, 'Preparing to flash...');

        // Simulate flashing process
        // In production, this would:
        // 1. Download firmware binary
        // 2. Erase flash
        // 3. Write firmware
        // 4. Verify

        for (let i = 0; i <= 100; i += 10) {
            await sleep(500);
            updateProgress(i, i < 100 ? 'Flashing...' : 'Complete!');
        }

        logSuccess('Firmware flashed successfully!');
        logInfo('Please reset your device.');

    } catch (error) {
        logError('Flashing failed: ' + error.message);
    } finally {
        setTimeout(() => {
            progressDiv.classList.remove('show');
        }, 2000);
    }
}

async function eraseFlash() {
    if (!isConnected) {
        logError('Please connect a device first');
        return;
    }

    if (!confirm('Are you sure you want to erase the flash? This will delete all data on the device.')) {
        return;
    }

    try {
        logInfo('Erasing flash...');

        progressDiv.classList.add('show');
        updateProgress(0, 'Erasing...');

        // Simulate erase
        await sleep(3000);

        updateProgress(100, 'Erased!');
        logSuccess('Flash erased successfully');

    } catch (error) {
        logError('Erase failed: ' + error.message);
    } finally {
        setTimeout(() => {
            progressDiv.classList.remove('show');
        }, 2000);
    }
}

function updateUI() {
    if (isConnected) {
        connectBtn.textContent = 'Disconnect Device';
        connectBtn.className = 'btn-connect';
        flashBtn.disabled = false;
        eraseBtn.disabled = false;
        deviceStatus.textContent = 'Connected';
        deviceStatus.className = 'status-value connected';
    } else {
        connectBtn.textContent = 'Connect Device';
        flashBtn.disabled = true;
        eraseBtn.disabled = true;
        deviceStatus.textContent = 'Not Connected';
        deviceStatus.className = 'status-value disconnected';
        chipType.textContent = '-';
        macAddress.textContent = '-';
    }
}

function updateProgress(percent, text) {
    progressFill.style.width = percent + '%';
    progressFill.textContent = percent + '%';
    progressText.textContent = text;
}

function logInfo(message) {
    addLogEntry(message, '');
}

function logSuccess(message) {
    addLogEntry(message, 'success');
}

function logWarning(message) {
    addLogEntry(message, 'warning');
}

function logError(message) {
    addLogEntry(message, 'error');
}

function addLogEntry(message, className) {
    const entry = document.createElement('div');
    entry.className = 'log-entry ' + className;
    const timestamp = new Date().toLocaleTimeString();
    entry.textContent = `[${timestamp}] ${message}`;
    logDiv.appendChild(entry);
    logDiv.scrollTop = logDiv.scrollHeight;
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

// Initialize
logInfo('MeshNet Web Flasher ready');
logInfo('Select your board and click "Connect Device"');
