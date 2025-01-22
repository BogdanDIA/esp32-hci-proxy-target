#!/bin/bash

esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 --before=default_reset --after=hard_reset erase_region 0x9000 0x6000 
