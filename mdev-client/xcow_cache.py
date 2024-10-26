#!/usr/bin/env python3

CLUSTER_SIZE = 65536
LBA_SIZE = 512
CLUSTER_LBAS = (CLUSTER_SIZE // LBA_SIZE)

XCACHE_SIZE = 32768
XCACHE_ASSOC = 4
XCACHE_LINES = (XCACHE_SIZE // XCACHE_ASSOC)

def vlba_resolve(vlba):
    vblk = (vlba) // CLUSTER_LBAS
    off = (vlba) % CLUSTER_LBAS
    tag = vblk // XCACHE_LINES
    idx = vblk % XCACHE_LINES
    return vblk, off, tag, idx

for vlba in range(0, 1024*1024, 128):
    vblk, off, tag, idx = vlba_resolve(vlba)
    cis = []
    for i in range(XCACHE_ASSOC):
        ci = idx * XCACHE_ASSOC + i
        cis.append(ci)
    print(vlba, cis)
