#!/bin/sh
for h in cumulus nvme-sgx multi0 multi1 multi2 multi3
do
    qemu-img create -f qcow2 -b nvmetro.qcow2 -F qcow2 ../vm-$h.qcow2
done
