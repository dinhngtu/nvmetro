#!/bin/sh -u
if ! ip link show br0 type bridge
then
    oldip=$(ip -4 -o address show dev eno1 | awk '{print $4}')
    ip link add br0 address $(cat /sys/class/net/eno1/address) type bridge
    ip link set br0 up
    ip address add dev br0 $oldip
    ip link set eno1 master br0
    ip address del $oldip dev eno1
fi
