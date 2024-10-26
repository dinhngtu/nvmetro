#!/bin/sh
# cd tools
# make bpf
# CLANG=clang-9 LLC=llc-9
#make -C tools/lib/bpf bpf_helper_defs.h
#make tools/bpf -j12
limake M=samples/bpf -j "$@"
