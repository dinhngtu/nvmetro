#!/bin/sh
set -eu
make -j USE_CLANG=0 USE_MIMALLOC_DYNAMIC=0
