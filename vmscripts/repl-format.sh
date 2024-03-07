#!/bin/sh
set -eu
. ./vars.sh

sync
echo 1 > /proc/sys/vm/drop_caches
nvme format -n 1 -l 0 /dev/spdk/nvme0
echo 1 > /proc/sys/vm/drop_caches
ssh root@$target_ip "sync && echo 1 > /proc/sys/vm/drop_caches && nvme format $target_localpath && echo 1 > /proc/sys/vm/drop_caches"
