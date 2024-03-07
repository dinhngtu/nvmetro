#!/bin/bash
set -e

. ./vars.sh

nvmedev=$1
shift

jq ".storage_objects[0].dev=\"$nvmeblk\"" iscsi_template.json > iscsi.json
modprobe vhost-scsi
targetctl restore iscsi.json 1>&2
