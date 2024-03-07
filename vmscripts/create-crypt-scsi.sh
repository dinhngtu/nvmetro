#!/bin/bash
set -e

nvmedev=$1
shift
cryptname=nvmecrypt
#shift

modprobe vhost-scsi

#if [ "$1" = "--format" ]
#then
    #nvme format $nvmedev 1>&2
    #cryptsetup luksFormat -c aes-xts-plain64 -s 256 -d keyfile -q $nvmedev 1>&2
#fi

cryptsetup open --type plain -c aes-xts-plain64 -s 256 -d keyfile -q $nvmedev nvmecrypt 1>&2
targetctl restore iscsi_$cryptname.json 1>&2
echo $cryptname
