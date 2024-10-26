#!/bin/bash -eu
. ./vars.sh
sync
echo 1 > /proc/sys/vm/drop_caches
nvme format $nvmeblk
echo 1 > /proc/sys/vm/drop_caches
sfdisk $nvmeblk < parttable-xcow.dump
$mdev_client/xcowctl -M $xcow_mapfile -o format -b $xcow_nvmeblk -z $xcow_fsize
