#!/bin/sh
set -e

pcidev=$1
shift

modprobe nvme-mdev 1>&2
uuid=$(uuidgen)
mdev=/sys/bus/mdev/devices/$uuid
echo $uuid > /sys/bus/pci/devices/$pcidev/mdev_supported_types/nvme-4Q_V1/create
echo 8 > $mdev/settings/oncs_mask
echo 16 > $mdev/settings/vq_count
iommu_group=$(basename $(readlink $mdev/iommu_group))
#echo -n "nqn.2014-08.org.nvmexpress:NVMf:uuid:9ea57fc0-83ae-4e14-89e3-db77d6691c21" > $mdev/settings/subnqn
#echo 0 > $mdev/settings/iothread_cpu
echo n1 > $mdev/namespaces/add_namespace
echo $uuid $mdev /dev/vfio/$iommu_group

if [ "$1" = "--bpf" ]
then
    bpf_path=/lib/modules/$(uname -r)/build/samples/bpf
    $bpf_path/nvme_mdev /dev/vfio/$iommu_group $uuid $bpf_path/$2.o 1>&2
fi
