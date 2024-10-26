#!/bin/bash
set -eu

. ./vars.sh

make_memfile
modprobe cuse
make_spdk
$spdk/scripts/rpc.py accel_crypto_key_create -c AES_XTS -k e9b699a03901741303b1bf3416c3c01e -e 7b3dcb3e5a9aeb219f498ab6c7ebdb6f -n encnvmekey
$spdk/scripts/rpc.py bdev_crypto_create -n encnvmekey nvme0n1 encnvme
$spdk/scripts/rpc.py vhost_create_blk_controller --cpumask $spdkcpus vhost.0 encnvme

trap "cleanup_spdk" EXIT
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -chardev socket,id=char0,path=/var/tmp/vhost.0 \
    -device vhost-user-blk-pci,chardev=char0,bootindex=2
