#!/usr/bin/env sh

# mount first
#mount -t 9p -o trans=virtio r /mnt

echo "Install NVDLA driver"
insmod /mnt/drm.ko
echo "Lihui: dump memory"
insmod /mnt/opendla.ko min_index=52 max_index=52 mem_dump=0 file_dump=1 dump_filter_on=1 dump_first=0

echo "Config SSH for regression envrionment"
sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config
sed -i 's/#PermitEmptyPasswords no/PermitEmptyPasswords yes/'  /etc/ssh/sshd_config
/etc/init.d/*sshd restart
