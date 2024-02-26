#!/bin/sh

set -eux

udc=$1

case $2 in
ff802)
	vid=0x0424
	pid=0x3fdd
	product='Fireface 802 (XXXXXXXX)'
	numports=2
	;;
ffucxii)
	vid=0x2a39
	pid=0x3fd9
	product='Fireface UCX II (XXXXXXXX)'
	numports=2
	;;
ffufxii)
	vid=0x2a39
	pid=0x3fd1
	product='Fireface UFX II (XXXXXXXX)'
	numports=3
	;;
*)
	vid=$2
	pid=$3
	product=$4
	numports=$5
	;;
esac

modprobe usb_f_midi
cd /sys/kernel/config/usb_gadget
mkdir g1
cd g1

echo "$vid" > idVendor
echo "$pid" > idProduct

mkdir strings/0x409
echo "$product" > strings/0x409/product

mkdir functions/midi.0
echo "$numports" > functions/midi.0/in_ports
echo "$numports" > functions/midi.0/out_ports

mkdir configs/c.1
ln -s functions/midi.0 configs/c.1/

echo "$udc" > UDC
