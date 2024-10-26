#!/bin/bash
set -eu

. ./vars.sh

$mdev_client/xcowctl -M $xcow_mapfile -o snap-create
