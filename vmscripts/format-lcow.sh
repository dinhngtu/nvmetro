#!/bin/bash -eu
. ./vars.sh
vgchange -an $l_vgname || true
sync
echo 1 > /proc/sys/vm/drop_caches
nvme format $nvmeblk
echo 1 > /proc/sys/vm/drop_caches
pvcreate -f $nvmeblk
vgcreate $l_vgname $nvmeblk
lvcreate -n lpoolmeta -L 10GiB lcow
lvcreate -n lpool -L 800GiB lcow
lvconvert -fy --type thin-pool --poolmetadata lcow/lpoolmeta lcow/lpool
lvcreate -V 100GiB --thinpool lpool -n $l_lv_base $l_vgname
time (echo -n "writing random... "; $mdev_client/writerand -b $l_path_base -z $(blockdev --getsize64 $l_path_base) -B 65536 -k ./keyfile -P 0.5 -j 6)
sync
lvchange -an $l_path_base
lvcreate -n $l_lv_top -s $l_path_base
lvchange -ay -K $l_path_top
