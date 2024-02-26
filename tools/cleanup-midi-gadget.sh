#!/bin/sh

set -eux

cd /sys/kernel/config/usb_gadget

echo > g1/UDC || :
rm -f g1/configs/c.1/midi.0
rmdir g1/configs/c.1 g1/functions/midi.0 g1/strings/0x409 g1
