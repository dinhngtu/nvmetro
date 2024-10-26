#!/bin/sh -eu
make modules_install -j12
make install
#sha256sum /boot/vmlinuz-5.10.66+ > ./vmscripts/vmlinuz.sha256
