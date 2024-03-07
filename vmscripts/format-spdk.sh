#!/bin/bash
set -eu
. ./vars.sh
sync
echo 1 > /proc/sys/vm/drop_caches
nvme format -n 1 -l 0 /dev/spdk/nvme0
echo 1 > /proc/sys/vm/drop_caches
