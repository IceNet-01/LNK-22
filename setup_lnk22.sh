#!/bin/bash

echo "=========================================="
echo "  LNK-22 Setup Script"
echo "=========================================="
echo ""
echo "This script will:"
echo "1. Copy Monocypher files (eliminates LGPL risk)"
echo "2. Update branding from MeshNet to LNK-22"
echo "3. Verify file structure"
echo ""

# Copy Monocypher files
echo "[1/3] Copying Monocypher files..."
cd /home/mesh/LNK-22/firmware/src/crypto

if [ -f "/home/mesh/Monocypher/src/monocypher.h" ]; then
    cp /home/mesh/Monocypher/src/monocypher.h ./
    cp /home/mesh/Monocypher/src/monocypher.c ./
    echo "✓ Monocypher files copied"
else
    echo "✗ Monocypher source not found at /home/mesh/Monocypher/"
    exit 1
fi

# Update file headers
echo "[2/3] Updating branding..."

# Update main.cpp
sed -i 's/MeshNet Firmware/LNK-22 Firmware/g' /home/mesh/LNK-22/firmware/src/main.cpp
sed -i 's/MESHNET_VERSION/LNK22_VERSION/g' /home/mesh/LNK-22/firmware/src/main.cpp

# Update config.h
sed -i 's/MESHNET_CONFIG_H/LNK22_CONFIG_H/g' /home/mesh/LNK-22/firmware/src/config.h
sed -i 's/MeshNet Configuration/LNK-22 Configuration/g' /home/mesh/LNK-22/firmware/src/config.h

# Update crypto.h
sed -i 's/MESHNET_CRYPTO_H/LNK22_CRYPTO_H/g' /home/mesh/LNK-22/firmware/src/crypto/crypto.h
sed -i 's/MeshNet Cryptography/LNK-22 Cryptography/g' /home/mesh/LNK-22/firmware/src/crypto/crypto.h

# Update platformio.ini
sed -i 's/MeshNet Firmware/LNK-22 Firmware/g' /home/mesh/LNK-22/firmware/platformio.ini
sed -i 's/MESHNET_VERSION/LNK22_VERSION/g' /home/mesh/LNK-22/firmware/platformio.ini

echo "✓ Branding updated"

# Verify
echo "[3/3] Verifying files..."
echo ""
echo "Monocypher files:"
ls -lh /home/mesh/LNK-22/firmware/src/crypto/monocypher.*

echo ""
echo "=========================================="
echo "  Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. Update crypto.cpp with Monocypher implementation"
echo "   - See /tmp/LNK22_IMPLEMENTATION_GUIDE.md"
echo ""
echo "2. Remove LGPL dependency from platformio.ini:"
echo "   - Remove line: rweather/Crypto@^0.4.0"
echo ""
echo "3. Add Monocypher to build:"
echo "   - Add to build_src_filter: +<crypto/monocypher.c>"
echo ""
echo "4. Build and test:"
echo "   - cd /home/mesh/LNK-22/firmware"
echo "   - pio run -e rak4631_full"
echo ""
echo "Full guide: /tmp/LNK22_IMPLEMENTATION_GUIDE.md"
echo "Strategic roadmap: /tmp/LNK-22_STRATEGIC_ROADMAP.md"
