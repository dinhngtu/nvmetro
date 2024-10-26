#!/bin/bash
set -eu

. ./vars.sh

make_memfile
make_spdk
$spdk/scripts/rpc.py vhost_create_blk_controller --cpumask $spdkcpus vhost.0 spool/svol

trap "cleanup_spdk" EXIT
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -chardev socket,id=char0,path=/var/tmp/vhost.0 \
    -device vhost-user-blk-pci,chardev=char0,bootindex=2
