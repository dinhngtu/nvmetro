#!/bin/bash
set -eu

. ./vars.sh

make_memfile

read scsi << EOF
$(./create-mirror.sh $nvmeblk)
EOF

trap "cleanup_mirror \"$scsi\"" EXIT
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -device vhost-scsi-pci,wwpn=naa.50014056c4798222,bootindex=2
