#!/bin/bash -eu
. ./vars.sh
sync
lastblk=$($mdev_client/xcowctl -M $xcow_mapfile -o snap-recreate)
blkdiscard -v -o $((65536*${lastblk})) $xcow_nvmeblk
