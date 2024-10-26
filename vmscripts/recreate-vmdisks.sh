#!/bin/bash
. ./vars.sh
for h in cumulus nvme-sgx multi0 multi1 multi2 multi3 nvmetro-ae-laptop
do
    $qemu_img create -f qcow2 -b nvmetro.qcow2 -F qcow2 ../vm-$h.qcow2
done
