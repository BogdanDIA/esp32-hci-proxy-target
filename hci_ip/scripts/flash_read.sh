#!/bin/bash
# Copyright BogdanDIA

BUILD_DIR=$(dirname "$0")/../build
IMG_DIR=$(dirname "$0")/../images

esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 --before=default_reset --after=hard_reset read_flash 0x9000 0x6000 ${IMG_DIR}/flash_read.bin  
