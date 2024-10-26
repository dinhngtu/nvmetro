# format:

```
mkfs.ext4 -E "nodiscard,lazy_itable_init=0" -O bigalloc,^has_journal -C 65536 /dev/nvme0n1
mkfs.ext4 -E "nodiscard,lazy_itable_init=0" -O ^has_journal /dev/nvme0n1
echo 3 > /proc/sys/vm/drop_caches
e2fsck -fn /dev/nvme0n1
```

# prep:

```
mount /dev/nvme0n1 /mnt
mkdir /mnt/thingy1
cp -r /boot /mnt/thingy1/boot1
cp -r /boot /mnt/thingy1/boot2
cp -r /boot /mnt/thingy1/boot3
cp -r /boot /mnt/thingy1/boot4
cp -r /mnt/thingy1 /mnt/thingy2
sync
cp -r /mnt/thingy1 /mnt/thingy3
cp -r /mnt/thingy1 /mnt/thingy4
rm -r /mnt/thingy4
cp -r /mnt/thingy2 /mnt/thingy5

umount /dev/nvme0n1
sync
echo 3 > /proc/sys/vm/drop_caches
```

# verify:

```
mount /dev/nvme0n1 /mnt
sha256sum /boot/{vmlinuz,initrd.img} /mnt/thingy*/boot*/{vmlinuz,initrd.img}
```

# fill:

```
# (in host)
scp writerand ../vmscripts/keyfile root@192.168.88.221:
# (in vm)
time (echo -n "writing random... "; ./writerand -b /dev/nvme0n1 -z $(blockdev --getsize64 /dev/nvme0n1) -B 65536 -k ./keyfile -P 0.5 -j 6)
# checkpoint
#nvme io-passthru -b -s -o 0x81 -n 1 /dev/nvme0n1
# checkpoint (cow)
poweroff
# (in host)
qemu-img create -f qcow2 -b /mnt/nvqcow/nvbase.qcow2 -F qcow2 /mnt/nvqcow/nv.qcow2 && fstrim /mnt/nvqcow && sync
```

# hang:

```
fio --bs=128k --rw=read --iodepth=1 --numjobs=1 --size=100g --ioengine=io_uring --hipri --direct=1 --exitall --name mdev --time_based --runtime=10 --filename=/dev/nvme0n1
fio --bs=128k --rw=read --iodepth=128 --numjobs=4 --size=100g --ioengine=io_uring --hipri --direct=1 --exitall --name mdev --time_based --runtime=10 --filename=/dev/nvme0n1
```
