#!/bin/bash -eu
. ./vars.sh

make_spdk
trap "cleanup_spdk" EXIT
$spdk/scripts/rpc.py bdev_lvol_snapshot spool/svol snap
