memsize=6G
nvmerepl=/dev/nvme1n1
mdev_client=../mdev-client
#mcinfo=./mcinfo
mcinfo=/dev/null
memfile=/mnt/huge0/mdev.mem
vmdisk=../vm-$(hostname).qcow2
#vmdisk=../nvmetro.qcow2
spdk=../spdk2309
laptop_hostname=nvmetro-ae-laptop

target_ip=192.168.0.3
target_localpath=/dev/nvme0n1

if [ "$(hostname)" = "$laptop_hostname" ]
then
guest_mac=ce:76:f8:2e:f1:6c
cpus=4
hcpus=2,3,6,7
mthreads=2
sgx_mthreads=1
# must match with mthreads
mcpus=0,1,4,5
spdkcpus=0x3
nvmeblk=/dev/nvme1n1
nvme=0000:70:00.0
else  # server
guest_mac=ce:76:f8:2e:f1:6b
cpus=8
hcpus=4,6,8,10,16,18,20,22
mthreads=2
# must match with mthreads
mcpus=0,2,12,14
spdkcpus=0x5
nvmeblk=/dev/nvme0n1
nvme=0000:41:00.0
fi

multi_memsize=2G
multi_io0=0
multi_guest_macs=(ce:76:f8:2e:f2:10 ce:76:f8:2e:f2:11 ce:76:f8:2e:f2:12 ce:76:f8:2e:f2:13)
multi_cpus=1
multi_hcpus=(4 6 8 10)
multi_mcpus=2
multi_vmdisks=(../vm-multi0.qcow2 ../vm-multi1.qcow2 ../vm-multi2.qcow2 ../vm-multi3.qcow2)
multi_nvmeblks=(/dev/nvme0n1p1 /dev/nvme0n1p2 /dev/nvme0n1p3 /dev/nvme0n1p4)

#xcow_mapfile=/tmp/xcow.map
xcow_mapfile=${nvmeblk}p1
xcow_nvmeblk=${nvmeblk}p2
xcow_fsize=$((100*2**30))

qcow_mnt=/mnt/nvqcow
qcow_base=$qcow_mnt/nvbase.qcow2
qcow_top=$qcow_mnt/nv.qcow2

l_vgname=lcow
l_lv_base=lbase
l_path_base=/dev/$l_vgname/$l_lv_base
l_lv_top=ltop
l_path_top=/dev/$l_vgname/$l_lv_top

cleanup_mdev() {
    echo -n "Cleaning up... "
    sleep 1
    rm -f $memfile || true
    kill $1 || true
    echo 1 > $2/remove || true
    echo "success"
}

cleanup_mdev_all() {
    echo -n "Cleaning up... "
    sleep 1
    rm -f $memfile || true
    kill $1 || true
    for mdev in /sys/bus/mdev/devices/*
    do
        echo 1 > $mdev/remove || true
    done
    echo "success"
}

cleanup_scsi() {
    echo -n "Cleaning up... "
    sleep 1
    rm -f $memfile || true
    targetctl clear || true
    echo "success"
}

cleanup_crypt() {
    echo -n "Cleaning up... "
    sleep 1
    rm -f $memfile || true
    targetctl clear || true
    cryptsetup close /dev/mapper/$1 || true
    echo "success"
}

cleanup_mirror() {
    echo -n "Cleaning up... "
    sleep 1
    rm -f $memfile || true
    targetctl clear || true
    dmsetup remove /dev/mapper/$1 || true
    echo "success"
}

cleanup_passthrough() {
    echo -n "Cleaning up... "
    sleep 1
    rm -f $memfile || true
    driverctl --nosave unset-override $nvme || true
    echo "success"
}

cleanup_spdk() {
    echo -n "Cleaning up... "
    sleep 1
    rm -f $memfile || true
    $spdk/scripts/rpc.py spdk_kill_instance SIGINT || true
    sleep 2
    driverctl --nosave unset-override $nvme || true
    echo "success"
}

cleanup_lvm() {
    echo -n "Cleaning up... "
    sleep 1
    rm -f $memfile || true
    targetctl clear || true
    vgchange -an nvmetro || true
    wipefs -a $nvmeblk || true
    echo "success"
}

cleanup_vtap() {
    echo -n "Cleaning up... "
    sleep 1
    ip link del vmtap
    echo "success"
}

make_memfile() {
    #memfile=$(mktemp -u /dev/shm/mdev.XXXXXXXXXX)
    truncate $memfile -s $memsize 1>&2
    echo $memfile
}

make_memfile_multi() {
    #memfile=$(mktemp -u /dev/shm/mdev.XXXXXXXXXX)
    truncate $1 -s $2 1>&2
    echo $1
}

make_passthrough() {
    driverctl --nosave set-override $nvme vfio-pci
}

make_spdk() {
    driverctl --nosave set-override $nvme vfio-pci
    sleep 2
    $spdk/build/bin/vhost -S /var/tmp -m $spdkcpus >$mcinfo 2>&1 &
    sleep 2
    $spdk/scripts/rpc.py bdev_nvme_attach_controller -b nvme0 -t pcie -a $nvme
    $spdk/scripts/rpc.py bdev_nvme_cuse_register -n nvme0
    sleep 2
}

make_vtap() {
    ip link add link br0 vmtap address $guest_mac type macvtap mode bridge
    ip link set vmtap up
    echo "/dev/tap$(cat /sys/class/net/vmtap/ifindex)"
}

qemu_bin="qemu-system-x86_64"
qemu="numactl -m 0 -C $hcpus -- $qemu_bin"
qemu_img="qemu-img"
vms="-name mdev-vm,debug-threads=on \
    -machine q35,accel=kvm,memory-backend=mem \
    -nodefaults \
    -cpu host \
    -smp $cpus \
    -m $memsize \
    -object memory-backend-file,id=mem,size=$memsize,mem-path=$memfile,share=on,discard-data=on,dump=off \
    -drive id=vmdisk,file=$vmdisk,format=qcow2,discard=on,if=none \
    -device virtio-blk-pci,drive=vmdisk,bootindex=1 \
    -serial mon:stdio \
    -monitor unix:/run/qemu/mon-mdev.sock,server,nowait \
    -qmp unix:/run/qemu/qmp-mdev.sock,server,nowait \
    -nographic"
nets="-nic bridge,br=br0,model=virtio-net-pci,mac=$guest_mac"
vtaps="-netdev tap,fd=3,id=vmtap,vhost=on \
    -device virtio-net-pci,netdev=vmtap,mac=$guest_mac"
