#!/bin/bash
set -eu
. ./vars.sh

make_spdk
trap "cleanup_spdk" EXIT
sync
echo 1 > /proc/sys/vm/drop_caches
nvme format -n 1 -l 0 /dev/spdk/nvme0
echo 1 > /proc/sys/vm/drop_caches
sleep 2
$spdk/scripts/rpc.py bdev_lvol_create_lvstore -c 65536 nvme0n1 spool --clear-method unmap
$spdk/scripts/rpc.py bdev_lvol_create -l spool svol 102400
# $spdk/scripts/rpc.py bdev_lvol_snapshot spool/svol snap
