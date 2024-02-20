# Capturing TotalMix FX traffic

Run the following to create a MIDI gadget that mirrors a UCX II.

```sh
UDC=fe980000.usb                          # raspberry pi 4 udc (see /sys/class/udc)

modprobe usb_f_midi                       # load midi gadget
cd /sys/kernel/config/usb_gadget
mkdir g1                                  # create gadget
cd g1

echo 0x2a39 > idVendor                    # set vendor id (RME)
echo 0x3fd9 > idProduct                   # set product id (Fireface UCX II)

mkdir functions/midi.usb0                 # add midi function
echo 2 > functions/midi.usb0/in_ports     # with 2 input ports
echo 2 > functions/midi.usb0/out_ports    # and 2 output ports

mkdir configs/c.1                         # add config
ln -s functions/midi.usb0 configs/c.1/    # attach midi function to config

echo "$UDC" > UDC                         # bind gadget to udc
```

The MIDI ports for this gadget will appear as `f_midi:0` and
`f_midi:1`. MIDI messages from TotalMix FX come through `f_midi:1`.
You can dump them with `aseqdump -p f_midi:1`.

Determine the client and port for your real device by finding it
in the output of `aconnect -l`. Again, we are interested in port 1.
On my system, it appeared as `32:1`. You can dump the traffic from
the device with `aseqdump -p 32:1`.

To connect the USB gadget with the real device, create connections
between these two ports (one in each direction):

```sh
aconnect f_midi:1 32:1
aconnect 32:1 f_midi:1
```

To remove the gadget, you can run the following:

```sh
echo '' > UDC                             # unbind from udc
rm configs/c.1/midi.usb0                  # detach midi function from config
rmdir configs/c.1 functions/midi.usb0     # remove config and midi function
cd ..
rmdir g1                                  # remove gadget
```
