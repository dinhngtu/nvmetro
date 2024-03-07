#!/bin/bash
set -eu

. ./vars.sh

make_memfile
make_passthrough

trap "cleanup_passthrough" EXIT
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -device vfio-pci,host=$nvme
