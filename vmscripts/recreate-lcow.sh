#!/bin/bash -eu
. ./vars.sh
lvremove -f -y $l_path_top
lvcreate -n $l_lv_top -s $l_path_base
lvchange -ay -K $l_path_top
