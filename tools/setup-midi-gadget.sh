#!/bin/sh

set -eux

udc=$1

case $2 in
ff802)
	product='Fireface 802 (XXXXXXXX)'
	numports=2
	;;
ffucxii)
	product='Fireface UCX II (XXXXXXXX)'
	numports=2
	;;
ffufxii)
	product='Fireface UFX II (XXXXXXXX)'
	numports=3
	;;
*)
	product=$2
	numports=$3
	;;
esac

modprobe usb_f_midi
cd /sys/kernel/config/usb_gadget
mkdir g1
cd g1

echo '0x1d6b' > idVendor  # Linux Foundation
echo '0x0104' > idProduct # Multifunction Composite Gadget

mkdir strings/0x409
echo "$product" > strings/0x409/product
# choose unique serial number
echo "$product:$numports" | cksum | awk '{printf "%.15d", $1}' > strings/0x409/serialnumber

mkdir functions/midi.0
echo "$numports" > functions/midi.0/in_ports
echo "$numports" > functions/midi.0/out_ports

mkdir configs/c.1
ln -s functions/midi.0 configs/c.1/

echo "$udc" > UDC
