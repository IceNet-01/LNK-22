#!/bin/bash
# Flash debug firmware to both radios

FIRMWARE="/home/mesh/LNK-22/firmware/lnk22-VERBOSE-DEBUG.uf2"

echo "=========================================="
echo "Flashing LNK-22 Debug Firmware to Both Radios"
echo "=========================================="

# Function to wait for bootloader
wait_for_bootloader() {
    local radio_num=$1
    echo ""
    echo "[$radio_num] Waiting for bootloader..."
    echo "[$radio_num] Please double-tap RESET button now!"

    for i in {1..30}; do
        if [ -d "/media/$USER/RAK4631" ] || [ -d "/media/mesh/RAK4631" ]; then
            echo "[$radio_num] Bootloader detected!"
            return 0
        fi
        sleep 1
        echo -n "."
    done

    echo ""
    echo "[$radio_num] Timeout waiting for bootloader"
    return 1
}

# Function to flash firmware
flash_firmware() {
    local radio_num=$1
    local mount_point=""

    # Find the bootloader mount point
    if [ -d "/media/$USER/RAK4631" ]; then
        mount_point="/media/$USER/RAK4631"
    elif [ -d "/media/mesh/RAK4631" ]; then
        mount_point="/media/mesh/RAK4631"
    else
        # Check other possible locations
        mount_point=$(mount | grep RAK4631 | awk '{print $3}' | head -1)
    fi

    if [ -z "$mount_point" ]; then
        echo "[$radio_num] ERROR: Cannot find RAK4631 bootloader mount point"
        return 1
    fi

    echo "[$radio_num] Copying firmware to $mount_point..."
    cp "$FIRMWARE" "$mount_point/"
    sync

    echo "[$radio_num] Firmware copied! Waiting for reset..."
    sleep 3

    echo "[$radio_num] Done!"
    return 0
}

# Flash Radio 1
echo ""
echo "====== FLASHING RADIO 1 ======"
wait_for_bootloader "Radio 1" && flash_firmware "Radio 1"

echo ""
echo "Wait 5 seconds before flashing Radio 2..."
sleep 5

# Flash Radio 2
echo ""
echo "====== FLASHING RADIO 2 ======"
wait_for_bootloader "Radio 2" && flash_firmware "Radio 2"

echo ""
echo "=========================================="
echo "Flashing complete!"
echo "Wait 10 seconds for both radios to boot..."
sleep 10

echo "Testing radios..."
python3 /home/mesh/LNK-22/quick_test.py

echo "Done!"
