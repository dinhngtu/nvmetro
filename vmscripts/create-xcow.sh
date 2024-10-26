#!/bin/bash
set -e

. ./vars.sh

pcidev=$1
shift

modprobe nvme-mdev 1>&2
uuid=$(uuidgen)
mdev=/sys/bus/mdev/devices/$uuid
echo $uuid > /sys/bus/pci/devices/$pcidev/mdev_supported_types/nvme-4Q_V1/create
#echo 8 > $mdev/settings/oncs_mask # enable write zeroes
echo 0 > $mdev/settings/oncs_mask
#echo 2 > $mdev/settings/vq_count
echo 1 > $mdev/settings/allow_vendor_adm
echo 1 > $mdev/settings/allow_vendor_io
iommu_group=$(basename $(readlink $mdev/iommu_group))
#echo -n "nqn.2014-08.org.nvmexpress:NVMf:uuid:9ea57fc0-83ae-4e14-89e3-db77d6691c21" > $mdev/settings/subnqn
#echo 0 > $mdev/settings/iothread_cpu
echo "${xcow_nvmeblk#/dev/nvme?}" > $mdev/namespaces/add_namespace
echo $uuid $mdev /dev/vfio/$iommu_group

if [ "$1" = "--bpf" ]
then
    bpf_path=/lib/modules/$(uname -r)/build/samples/bpf
    $bpf_path/nvme_mdev /dev/vfio/$iommu_group $uuid $bpf_path/$2.o 1>&2
fi
