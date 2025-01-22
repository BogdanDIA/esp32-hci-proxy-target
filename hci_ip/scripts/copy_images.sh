#!/bin/bash
# Copyright BogdanDIA

BUILD_DIR=$(dirname "$0")/../build
IMG_DIR=$(dirname "$0")/../images

cp ${BUILD_DIR}/bootloader/bootloader.bin $IMG_DIR
cp ${BUILD_DIR}/hci_ip.bin $IMG_DIR 
cp ${BUILD_DIR}/partition_table/partition-table.bin $IMG_DIR
