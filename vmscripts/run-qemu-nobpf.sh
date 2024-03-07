#!/bin/bash
set -eu

. ./vars.sh

make_memfile

read uuid mdev iommu_path << EOF
$(./create-mdev.sh $nvme)
EOF

sleep infinity &
mcjob=$!

trap "cleanup_mdev \"$mcjob\" \"$mdev\"" EXIT
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -device vfio-pci,sysfsdev=$mdev
