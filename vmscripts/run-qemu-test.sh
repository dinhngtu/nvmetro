#!/bin/bash
set -eu

. ./vars.sh

make_memfile

sleep 1
mkdir -p /run/qemu

$qemu $vms $nets
