#!/bin/sh
set -e
. ./vars.sh
ssh root@$target_ip "/root/nvmetcli/nvmetcli restore"
sleep 2
sudo nvme connect-all
