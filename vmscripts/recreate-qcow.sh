#!/bin/bash -eu
. ./vars.sh
rm -f $qcow_top
$qemu_img create -f qcow2 -b $qcow_base -F qcow2 $qcow_top
sync
fstrim -v $qcow_mnt
sync
