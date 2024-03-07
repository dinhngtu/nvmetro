#!/bin/bash
set -eu

. ./vars.sh
prog_kern=nmbpf_replicate

make_memfile

read uuid mdev iommu_path << EOF
$(./create-mdev.sh $nvme --bpf $prog_kern)
EOF

numactl -m 0 -C $mcpus $mdev_client/replicator-aio -g $iommu_path -d $uuid -m $memfile -j $mthreads -b $nvmerepl >$mcinfo 2>&1 &
#heaptrack $mdev_client/replicator-aio -g $iommu_path -d $uuid -m $memfile -j $mthreads -b $nvmerepl >$mcinfo 2>&1 &
mcjob=$!

trap "cleanup_mdev \"$mcjob\" \"$mdev\"" EXIT
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -device vfio-pci,sysfsdev=$mdev
