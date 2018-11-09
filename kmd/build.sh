#!/bin/sh

make KDIR=/home/lh/buildroot/output/build/linux-4.13.3/ ARCH=arm64 CROSS_COMPILE=/home/lh/buildroot/output/host/bin/aarch64-linux-gnu- $1
#make KDIR=/home/lh/linux-plnx_aarch64-standard-build/ ARCH=arm64 CROSS_COMPILE=/home/lh/buildroot/output/host/bin/aarch64-linux-gnu- $1

#cho 'copying opendla.ko to /mnt/share...'
#p ./port/linux/opendla.ko /mnt/share/
#cho 'done'
