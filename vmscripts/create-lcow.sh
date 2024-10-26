#!/bin/bash
set -eu

modprobe vhost-scsi
targetctl restore iscsi_lcow.json 1>&2
