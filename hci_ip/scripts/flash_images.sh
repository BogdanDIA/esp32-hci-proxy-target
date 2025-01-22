#!/bin/bash
# Copyright BogdanDIA

BUILD_DIR=$(dirname "$0")/../build
IMG_DIR=$(dirname "$0")/../images

esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 ${IMG_DIR}/bootloader.bin 0x10000 ${IMG_DIR}/hci_ip.bin 0x8000 ${IMG_DIR}/partition-table.bin
