#!/bin/bash
set -eu

. ./vars.sh
# encrypt_ip doesn't work because of page cache corruption
prog_kern=nmbpf_encrypt_kern

g=
d=
m=
b=
memfiles=()
mdevs=()
for i in 0 1 2 3
do
    memfiles[$i]="$memfile.$i"
    make_memfile_multi "${memfiles[$i]}" $multi_memsize

    read uuid mdev iommu_path

    d="$d -d $uuid"
    mdevs[$i]="$mdev"
    g="$g -g $iommu_path"

    m="$m -m ${memfiles[$i]}"
    b="$b -b ${multi_nvmeblks[$i]}"
done < <(./create-multi.sh $nvme --bpf $prog_kern)

numactl -m 0 -C $multi_mcpus $mdev_client/encryptor-multi $g $d $m $b -k keyfile -j 1 >$mcinfo 2>&1 &
mcjob=$!

trap "cleanup_mdev_all \"$mcjob\"" EXIT

sleep 1
mkdir -p /run/qemu

qjobs=
for i in 0 1 2 3
do
    hcpus="${multi_hcpus[$i]}"
    cpus="$multi_cpus"
    memfile="${memfiles[$i]}"
    memsize=$multi_memsize
    vmdisk="${multi_vmdisks[$i]}"
    guest_mac="${multi_guest_macs[$i]}"
    mdev="${mdevs[$i]}"

    echo "guest_mac $guest_mac"

    qemu="numactl -m 0 -C $hcpus -- $qemu_bin"
    vms="-name mdev-vm$i,debug-threads=on \
        -machine q35,accel=kvm,memory-backend=mem \
        -nodefaults \
        -cpu host \
        -smp $cpus \
        -m $memsize \
        -object memory-backend-file,id=mem,size=$memsize,mem-path=$memfile,share=on,discard-data=on,dump=off \
        -drive id=vmdisk,file=$vmdisk,format=qcow2,discard=on,if=none \
        -device virtio-blk-pci,drive=vmdisk,bootindex=1 \
        -monitor unix:/run/qemu/mon-mdev$i.sock,server,nowait \
        -qmp unix:/run/qemu/qmp-mdev$i.sock,server,nowait \
        -nographic"
    nets="-nic bridge,br=br0,model=virtio-net-pci,mac=$guest_mac"

    if [ $i = "0" ]
    then
        $qemu $vms $nets -device vfio-pci,sysfsdev=$mdev -serial mon:stdio &
    else
        $qemu $vms $nets -device vfio-pci,sysfsdev=$mdev &
    fi
    qjobs="$qjobs $!"
done

wait $qjobs
