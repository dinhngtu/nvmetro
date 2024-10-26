#!/bin/bash -eu
. ./vars.sh
modprobe nbd
qemu-nbd -d /dev/nbd0
mkdir -p $qcow_mnt
umount $qcow_mnt || true
sync
echo 1 > /proc/sys/vm/drop_caches
nvme format $nvmeblk
echo 1 > /proc/sys/vm/drop_caches
mkfs.ext4 -E "nodiscard,lazy_itable_init=0" -O bigalloc,^has_journal -C 65536 $nvmeblk
mount $nvmeblk $qcow_mnt
$qemu_img create -f qcow2 -o cluster_size=65536 $qcow_base $xcow_fsize
