#!/bin/sh
set -e

pcidev=$1
shift

modprobe nvme-mdev 1>&2
#taskset -p $multi_io0 $(pidof nvme_mdev_poll0) 1>&2

for i in 0 1 2 3
do
    uuid=$(uuidgen)
    mdev=/sys/bus/mdev/devices/$uuid
    echo $uuid > /sys/bus/pci/devices/$pcidev/mdev_supported_types/nvme-1Q_V1/create
    echo 8 > $mdev/settings/oncs_mask
    echo 16 > $mdev/settings/vq_count
    iommu_group=$(basename $(readlink $mdev/iommu_group))
    echo n1p$(expr $i + 1) > $mdev/namespaces/add_namespace
    echo $uuid $mdev /dev/vfio/$iommu_group

    if [ "$1" = "--bpf" ]
    then
        bpf_path=/lib/modules/$(uname -r)/build/samples/bpf
        $bpf_path/nvme_mdev /dev/vfio/$iommu_group $uuid $bpf_path/$2.o 1>&2
    fi
done
