#!/bin/bash
set -eu

. ./vars.sh

make_memfile

read scsi << EOF
$(./create-lcow.sh)
EOF

trap "cleanup_scsi" EXIT
sleep 1
mkdir -p /run/qemu

$qemu $vms $nets \
    -device vhost-scsi-pci,wwpn=naa.5001405c77f05226,bootindex=2
