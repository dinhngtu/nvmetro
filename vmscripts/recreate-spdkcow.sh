#!/bin/bash -eu
. ./vars.sh

make_spdk
trap "cleanup_spdk" EXIT
sleep 5
$spdk/scripts/rpc.py -t 1200 bdev_lvol_delete spool/svol
$spdk/scripts/rpc.py bdev_lvol_clone spool/snap svol
date
tput bel
sleep 1.2
tput bel
