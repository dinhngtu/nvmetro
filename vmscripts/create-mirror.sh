#!/bin/bash
set -e

. ./vars.sh

cryptname=nvmecrypt

modprobe vhost-scsi
dmsetup create $cryptname --table "0 $(cat /sys/block/$(basename $nvmeblk)/size) mirror core 2 16384 nosync 2 $nvmeblk 0 $nvmerepl 0"
targetctl restore iscsi_$cryptname.json 1>&2
echo $cryptname
