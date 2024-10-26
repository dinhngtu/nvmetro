echo git clone https://github.com/spdk/spdk.git -b v23.09 --depth=1 --shallow-submodules --recurse-submodules spdk2309
./configure --with-crypto --with-xnvme --with-vhost --with-virtio --with-uring --with-nvme-cuse
