#!/bin/bash -eu
. ./vars.sh
sync
echo 1 > /proc/sys/vm/drop_caches
nvme format $nvmeblk
echo 1 > /proc/sys/vm/drop_caches
