#!/bin/bash
set -eu

. ./vars.sh

make_memfile

sleep 1
mkdir -p /run/qemu

# don't use hcpus, let qemu run on all cpus
numactl -m 0 -- $qemu_bin $vms $nets \
    -drive id=nvmeblk,file=$qcow_base,format=qcow2,discard=on,if=none,cache=none,aio=io_uring \
    -device virtio-blk-pci,drive=nvmeblk,bootindex=2
