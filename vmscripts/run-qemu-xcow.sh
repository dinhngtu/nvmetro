#!/bin/bash
set -e

. ./vars.sh
prog_kern=nmbpf_xcow

ulimit -l 524288

make_memfile

read uuid mdev iommu_path << EOF
$(./create-xcow.sh $nvme --bpf $prog_kern)
EOF

if [ "$1" = "-F" ] || [ ! -e "$xcow_mapfile" ]
then
    if [ ! -b "$xcow_mapfile" ]
    then
        rm -f $xcow_mapfile
        truncate -s 2G $xcow_mapfile || true
    fi
    numactl -m 0 -C $mcpus $mdev_client/xcowsrv -g $iommu_path -d $uuid -m $memfile -M $xcow_mapfile -F -j 1 -b $xcow_nvmeblk >$mcinfo 2>&1 &
else
    numactl -m 0 -C $mcpus $mdev_client/xcowsrv -g $iommu_path -d $uuid -m $memfile -M $xcow_mapfile -j 1 -b $xcow_nvmeblk >$mcinfo 2>&1 &
fi
mcjob=$!

trap "cleanup_mdev \"$mcjob\" \"$mdev\"" EXIT
#echo $mcjob
#read
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -device vfio-pci,sysfsdev=$mdev
