#!/bin/bash

echo "=========================================="
echo "  MeshNet Full Firmware - DFU Upload"
echo "=========================================="
echo ""
echo "This will upload the complete MeshNet firmware"
echo "Features: LoRa Radio, Mesh Networking, Display, GPS, Crypto"
echo ""
echo "Please DOUBLE-PRESS the RESET button on your WisMesh Pocket v2 NOW"
echo ""
echo "Watching for new USB drives..."
echo ""

# Record current drives
BEFORE=$(lsblk -o NAME | tail -n +2)

sleep 3

# Check for new drives every second
for i in {1..60}; do
    AFTER=$(lsblk -o NAME | tail -n +2)
    NEW=$(comm -13 <(echo "$BEFORE" | sort) <(echo "$AFTER" | sort))

    if [ ! -z "$NEW" ]; then
        echo "✓ New device detected: $NEW"

        # Wait a moment for it to mount
        sleep 2

        # Find where it's mounted
        MOUNT=$(lsblk -o NAME,MOUNTPOINT | grep "$NEW" | awk '{print $2}')

        if [ ! -z "$MOUNT" ]; then
            echo "✓ Mounted at: $MOUNT"
            echo "Copying full firmware..."

            sudo cp /home/mesh/MeshNet/firmware/meshnet-full.uf2 "$MOUNT/" 2>&1

            if [ $? -eq 0 ]; then
                echo "✓ Full firmware copied!"
                sync
                echo "Device will reboot automatically"
                echo ""
                echo "After boot you should see:"
                echo "  - Display showing MeshNet interface"
                echo "  - Serial output at 115200 baud"
                echo "  - Node address and network status"
                echo ""
                echo "Connect via: sudo screen /dev/ttyACM0 115200"
                exit 0
            fi
        else
            echo "Drive not mounted yet, will try to mount it..."
            DEVNAME=$(echo "$NEW" | head -1)

            # Create mount point
            sudo mkdir -p /mnt/rak_dfu
            sudo mount /dev/"$DEVNAME" /mnt/rak_dfu 2>&1

            if [ $? -eq 0 ]; then
                echo "✓ Mounted at /mnt/rak_dfu"
                sudo cp /home/mesh/MeshNet/firmware/meshnet-full.uf2 /mnt/rak_dfu/
                sync
                sudo umount /mnt/rak_dfu
                echo "✓ Full firmware uploaded!"
                echo ""
                echo "After boot you should see:"
                echo "  - Display showing MeshNet interface"
                echo "  - Serial output at 115200 baud"
                echo "  - Node address and network status"
                echo ""
                echo "Connect via: sudo screen /dev/ttyACM0 115200"
                exit 0
            fi
        fi
    fi

    printf "."
    sleep 1
done

echo ""
echo "✗ No new drive detected"
echo "Please double-press the reset button and try again"
