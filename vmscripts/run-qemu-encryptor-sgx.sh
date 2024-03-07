#!/bin/bash
set -eu

. ./vars.sh
# encrypt_ip doesn't work because of page cache corruption
prog_kern=nmbpf_encrypt_kern

make_memfile

read uuid mdev iommu_path << EOF
$(./create-mdev.sh $nvme --bpf $prog_kern)
EOF

#/usr/bin/env LD_LIBRARY_PATH=/opt/intel/sgxsdk/lib64 $mdev_client/encryptor-sgx -g $iommu_path -d $uuid -m $memfile -k keyfile -j $mthreads -b $nvmeblk >$mcinfo 2>&1 &
numactl -m 0 -C $mcpus $mdev_client/encryptor-sgx-aio -g $iommu_path -d $uuid -m $memfile -k keyfile -j $sgx_mthreads -b $nvmeblk >$mcinfo 2>&1 &
mcjob=$!

trap "cleanup_mdev \"$mcjob\" \"$mdev\"" EXIT
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -device vfio-pci,sysfsdev=$mdev
