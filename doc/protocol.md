# RME Fireface UCX II protocol

## Overview

This document describes the protocol used to communicate with an
RME Fireface UCX II over USB. The primary focus is when the interface
is is class-compliant mode, but some details about the normal USB
mode are mentioned as well.

The protocol details were obtained by inspecting USB traffic between
TotalMix FX (version 1.86) and a UCX II. For CC-mode, the iPad
version of TotalMix FX was used; a Raspberry Pi and Linux's USB
gadget MIDI function were used to capture the SysEx packets. See
[capture.md](capture.md) for more details.

This document is not official in any way, and the information
presented here may be subtly off, or just plain wrong.

## Class-compliant mode

Class-compliant mode uses the "Port 1" MIDI port to control the
device using System Exclusive packets. Each packet has the following
format:

	      Manufacturer ID
	      |
	   /------\
	F0 00 20 0D 10 <subid> <payload> F7
	|           |                    |
	SysEx start Device ID            SysEx end

The payload consists of a sequence of 32-bit integers. SysEx packets
can only contain bytes with the high bit unset, so each 32-bit
integer in the payload is encoded as five 7-bit bytes in little
endian.

For example, the number 0x1A2B3C4D is encoded as follows:


	1A       2B       3C       4D
	00011010 00101011 00111100 01001101
	
	0001 1010001 0101100 1111000 1001101
	01   51      2C      78      4D
	
	4D 78 2C 51 01

### Registers

Sub ID 0 is used by the host to set registers, and by the device
to notify the host of changes to registers. In this case, each
32-bit integer in the payload contains the register in bits 16-30,
and the value in the lower 16 bits. The high bit is used as a parity:
it is set if and only if the number of set bits in the register and
value is even.

#### Example

To enable mute of output channel 3, we need to set register 0x0582
to value 0x0001. There are 5 set bits in these integers, so we do
*not* set the high parity bit. Thus, we encode as a single 32-bit
integer 0x05820001, which is then encoded as a sysex payload:

	05       82       00       01
	00000101 10000010 00000000 00000001
	
	0000 0101100 0001000 0000000 0000001
	00   2C      08      00      01

Now, assembling the sysex packet with sub ID 0:

	F0 00 20 0D 10 00 01 00 08 2C 00 F7
	               |  \---Payload--/
	               Sub ID

### Levels

Sub ID 2 is used by the host with an empty payload to request current
level information. In response, the device sends packets with sub
IDs 1-5, each containing level information. The format of data in
the level packets is as follows:

	[<channel 1 rms low 32 bits> <channel 1 rms high 32 bits> <channel 1 peak>]...

Sub IDs 1 and 4 contain levels for the input channels (post-FX and
pre-FX respectively), sub IDs 3 and 5 contain levels for output
channels (pre-fader/FX and post-fader/FX), and sub ID 2 contains
levels for the playback channels.

Peak levels are given as a 32-integer with the following format:

	[0:3]	0xF if overload, otherwise 0
	[4:27]	24-bit peak level
	[28:31]	unused

RMS levels are not given directly. Instead, we are given a 64-bit
number containing the mean of the squares of the peak levels shifted
left by 4 (as above), multiplied by 2 so that the RMS levels of a
sine wave matches the peak levels.

### Output loopback

Sub ID 3 is used by the host to enable or disable loopback mode for
output channels. The payload contains a single 32-bit integer with
the following format:

	[0:6]	output channel
	[7]	loopback enabled
	[8:31]	unused

### EQ+D record

Sub ID 4 is used by the host to enable or disable EQ+D in recordings.
The payload is a single 32-bit integer with the following format:

	[0]	eq+d enabled
	[1:31]	unused

## USB-mode

### Registers

In USB-mode, registers are set by the host through bulk endpoint
12, and the device notifies the host of updates through bulk endpoint
13. In either case, the bulk data consists of a sequence of 32-bit
integers.

### Clock/hardware settings

Unlike CC-mode, clock and hardware settings are set through control
requests rather than writing to a bulk endpoint.

	bRequest=0x4010 wIndex=0x5cf wValue=
		[0:3]	clock type		0=internal 2=word 4=spdif coax 6=aes 8=optical
		[6]	word clock single speed	0=off 1=on
		[7]	spdif format		0=consumer 1=professional
		[8]	eq+d for record		0=off 1=on
		[10]	optical output		0=adat 1=spdif

	bRequest=0x4010 wIndex=0x30 wValue=
		0x00	32000/12800
		0x10	44100/88200/176400
		0x20	48000/96000/192000
		0x00	64000

## Registers

```
0000	input settings
	0000	mic 1
		0000	mute		0=off, 1=on
		0001	fxsend		int*10
		0002	stereo		0=off, 1=on
		0003	record		0=off, 1=on
		0004	?		all channels set to 0xa
		0005	play channel	0=off, 1-60
		0006	msproc		0=off, 1=on
		0007	phase		0=off, 1=on
		0008	gain		int*10, inst/line=0.0-24.0, mic=0.0-75.0
		0009	mic=48v		0=off, 1=on, line=level 0=+13dBu, 1=+19dBu
		000a	autoset		0=off, 1=on
		000b	n/a
		000c	lowcut		0=off, 1=on
			000d	freq		int16, 20Hz-500Hz
			000e	dB/oct		0=6, 1=12, 2=18, 3=24
		000f	eq		0=off, 1=on
			0010	band 1 type	0=peak, 1=shelf
			0011	band 1 gain	int*10, int16, -20.0dB-20.0dB
			0012	band 1 freq	int, 20Hz-20000Hz
			0013	band 1 q	int*10, 0.7-5.0
			0014	band 2 gain	int*10, int16, -20.0dB-20.0dB
			0015	band 2 freq	int, 20Hz-20000Hz
			0016	band 2 q	int*10, 0.7-5.0
			0017	band 3 type	0=peak, 1=shelf, 2=hicut
			0018	band 3 gain	int*10, int16, -20.0dB-20.0dB
			0019	band 3 freq	int, 20Hz-20000Hz
			001a	band 3 q	int*10, 0.7-5.0
		001b	dynamics		0=off, 1=on
			001c	gain		gain*10, int16, -30.0dB-30.0dB
			001d	attack		int, 0ms-200ms
			001e	release		int, 100ms-999ms
			001f	compthres	int*10, -60.0dB-0.0dB
			0020	compratio	int*10, 1.0-10.0
			0021	expthres	int*10, -99.0dB--20.0dB
			0022	expratio	int*10, 1.0-10.0
		0023	autolevel	0=off, 1=on
			0024	maxgain		int*10, 0.0dB-18.0dB
			0025	headroom	int*10, 3dB-12dB
			0026	risetime	int*10, 0.1s-9.9s
	0040	mic 2
		...
	0080	inst 3
		...
		0088	gain		int*10, 0.0dB-24.0dB
		0089	level		0=+13dBu, 1=+19dBu
		008b	hi-z		0=off, 1=on
		...
	00c0	inst 4
		...
	0100	line 5
		...
		010b	n/a
		...
	0140	line 6
	0180	line 7
	01c0	line 8
	0200	spdif 9
		...
		0208	n/a
		0209	n/a
		...
	0240	spdif 10
	0280	aes 11
	02c0	aes 12
	0300	adat 13
	0340	adat 14
	0380	adat 15
	03c0	adat 16
	0400	adat 17
	0440	adat 18
	0480	adat 19
	04c0	adat 20
0500	output settings
	0500	line 1
		0500	volume		int*10, -650=-inf, -65.0dB-6.0dB, step=0.5dB
		0501	balance		-100-100 (L100-0-R100)
		0502	mute		0=off, 1=on
		0503	fxreturn	-650=-inf, int*10, -64.5dB-0.0dB, step=0.5dB
		0504	stereo		0=off, 1=on, extra data?
		0505	record		0=off, 1=on
		0506	?		all channels set to 0xa
		0507	playchannel	0=off, 1-60
		0508	phase		0=off, 1=on, extra data
		0509	ref level	0=+4dBu, 1=+13dBu, 2=+19dBu
		050a	crossfeed	int, 0=off, 1-5
		050b	volume cal.	int*100, -24.0dB-3.0dB
		050c	lowcut		0=off, 1=on
			050d	freq		int, 20Hz-500Hz (step?)
			050e	dB/oct		0=6, 1=12, 2=18, 3=24
		050f	eq		0=off, 1=on
			0510	band1type	0=peak, 1=shelf
			0511	band1gain	int*10, -20.0dB-20.0dB
			0512	band1freq	int, 20Hz-20000Hz
			0513	band1q		int*10, 0.7-5.0
			0514	band2gain	int*10, -20.0dB-20.0dB
			0515	band2freq	int, 20Hz-20000Hz
			0516	band2q		int*10, 0.7-5.0
			0517	band3type	0=peak, 1=shelf, 2=hicut
			0518	band3gain	int*10, -20.0dB-20.0dB
			0519	band3freq	int, 20Hz-20000Hz
			051a	band3q		int*10, 0.7-5.0
		051b	dynamics		0=off, 1=on
			051c	gain		int*10, -30.0dB-30.0dB
			051d	attack		int, 0ms-200ms
			051e	release		int, 100ms-999ms
			051f	compthres	int*10, -60.0dB-0.0dB
			0520	compratio	int*10, 1.0-10.0
			0521	expthres	int*10, -99.0dB--20.0dB
			0522	expratio	int*10, 1.0-10.0
		0523	autolevel	0=off, 1=onn
			0524	maxgain		int*10, 0.0dB-18.0dB
			0525	headroom	int*10, 3dB-12dB
			0526	risetime	int*10, 0.1s-9.9s
	0540	line 2
		...
	0580	line 3
	05c0	line 4
	0600	line 5
	0640	line 6
	0680	phones 7
		...
		0689	ref level	0=+4dBu, 1=+19dBu
		...
	06c0	phones 8
	0700	spdif 9
		...
		0709	n/a
		...
	0740	spdif 10
	0780	aes 11
	07c0	aes 12
	0800	adat 13
	0840	adat 14
	0880	adat 15
	08c0	adat 16
	0900	adat 17
	0940	adat 18
	0980	adat 19
	09c0	adat 20
2000	mixer label
	2000	mix 1
		2000	input 1 vol/pan		signed 15-bit integer, if high bit 0, vol*10 or -300 if -inf, if high bit 1, pan -100-100
		2001	input 2 vol/pan
		...
		2013	input 20 vol/pan
	2040	mix 2
		...
	24c0	mix 20
2fc0	?	sent when register dump finished
3000	reverb		0=off, 1=on
	3001	type		int, 0=Small Room, 1=Medium Room, 2=Large Room, 3=Walls,
				     4=Shorty, 5=Attack, 6=Swagger, 7=Old School,
				     8=Echoistic, 9=8plus9, 10=Grand Wide, 11=Thicker,
				     12=Envelope, 13=Gated, 14=Space
	3002	predelay	int, 0ms-999ms
	3003	lowcut		int, 20Hz-500Hz
	3004	room scale	int*100, 0.50-3.00 (rooms only)
	3005	attack time	int, 5ms-400ms (envelope only)
	3006	hold time	int, 5ms-400ms (envelope and gated only)
	3007	release time	int, 5ms-500ms (envelope and gated only)
	3008	high cut	int, 2kHz-20kHz
	3009	reverb time	int*10, 0.1s-4.9s (space only)
	300a	damp		int, 2kHz-20kHz (space only)
	300b	smooth		int, 0-100%
	300c	volume		int*10, -65.0dB-6.0dB, -650=-inf
	300d	width		int*100, 0.00-1.00
3014	echo		0=off, 1=on
	3015	echo type	0=Stereo Echo, 1=Stereo Cross, 2=Pong Echo
	3016	delay time	int*1000, 0.000s-2.000s
	3017	feedback	int, 0-100%
	3018	high cut	0=off, 1=16kHz, 2=12kHz, 3=8kHz, 4=4kHz, 5=2kHz
	3019	volume		int*10, -65.0dB-6.0dB, -650=-inf
	301a	stereo width	int*100, 0.00-1.00
3050	control room
	3050	main out	0=1/2, 1=3/4, ..., 9=19/20
	3051	main mono	0=off, 1=on
	3052	phones source?
	3053	mute enable	0=off, 1=on
	3054	dim reduction	int*10	-65.0dB-0.0dB
	3055	dim		0=off, 1=on
	3056	recall volume	int*10	-65.0dB-0.0dB
3060	clock [CC-only]
	3064	clock source		0=internal 1=word clock 2=spdif 3=aes 4=optical
	3065	sample rate		0=32000 1=44100 2=48000 3=64000 4=88200 5=96000 6=128000 7=176400 8=192000 [R]
	3066	word clock out		0=off 1=on
	3067	word clock single speed	0=off 1=on
	3068	word clock termination	0=off 1=on
3070	hardware
	3078	optical out		0=adat 1=spdif
	3079	spdif format		0=consumer 1=professional
	307a	cc mode			0=off 1=on (only changes ui)
	307b	cc mix			0=totalmix 1=6ch+phones 2=8ch 3=20ch
	307c	standalone midi		0=off 1=on
	307d	standalone arc		0=volume 1=1s op 2=normal
	307e	lock keys		0=off 1=keys 2=all
	307f	remap keys		0=off 1=on
	3080	fx load	[R]
		[0:7]	fx load
		[8:15]	dsp version
	3081	?
	3082	set by device many times a second
	3083	?
3180	compression levels	[R]
	3180	input 1/2
		[0:7]
		[8:15]
	3181	input 3/4
	3182	input 5/6
	3183	input 7/8
	3184	input 9/10
	3185	input 11/12
	3186	input 13/14
	3187	input 15/16
	3188	input 17/18
	3189	input 19/20
	318a	output 1/2
	318b	output 3/4
	318c	output 5/6
	318d	output 7/8
	318e	output 9/10
	318f	output 11/12
	3190	output 13/14
	3191	output 15/16
	3192	output 17/18
	3193	output 19/20
3200	channel names		[RW]
	3200	input 1 name (null terminated, max 11 chars)
		3200	[0:1]
		3201	[2:3]
		3202	[4:5]
		3203	[6:7]
		3204	[8:9]
		3205	[10:11]
	3208	input 2 name
	3210	input 3 name
	...
	3290	input 19 name
	3298	input 20 name

	32a0	output 1 name
	32a8	output 2 name
	32b0	output 3 name
	...
	3338	output 20 name
3380	autolevels		[R]
	3380	input 1/2
		[0:7]
		[8:15]
	3381	input 3/4
	3382	input 5/6
	3383	input 7/8
	3384	input 9/10
	3385	input 11/12
	3386	input 13/14
	3387	input 15/16
	3388	input 17/18
	3389	input 19/20
	338a	output 1/2
	338b	output 3/4
	338c	output 5/6
	338d	output 7/8
	338e	output 9/10
	338f	output 11/12
	3390	output 13/14
	3391	output 15/16
	3392	output 17/18
	3393	output 19/20
3580	durec status		[R]
	3580
		[0:3]	0=no media 2=initializing 3=deleting? 5=stopped 6=recording a=playing
		[4:7]	?
		[8:15]	progress bar, 00=0% 41=100%
	3581	playback/record position
	3582	?
	3583	usb load/errors
		[0:7]	errors
		[8:16]	load %
	3584	total space	GB*16
	3585	free space	GB*16
	3586	total tracks
	3587	selected track
	3588	next track/play mode
		[0:11]	next track, fff=none
		[12:15]	play mode	0=single 1=UFX single 2=continuous 3=single next 4=repeat single 5=repeat all
	3589	remaining record time
	358a	track number, 0xffff=name of last played track and pending record tracks/sample rate
	358b	filename[0:1]
	358c	filename[2:3]
	358d	filename[4:5]
	358e	filename[6:7]
	358f	[0:7]=sample rate enum [8:15]=channels
	3590	track length
35d0	room eq
	35d0	output 1
		35d0	delay		int*100, 0.00ms-42.5ms
		35d1	enable		0=off 1=on
		35d2	band 1 type	0=peak 1=shelf 2=high pass 3=low pass
		35d3	band 1 gain	int*10, -20.0dB-20.0dB
		35d4	band 1 freq	int, 20Hz-20000Hz
		35d5	band 1 q	int*10, 0.4-9.9
		35d6	band 2 gain	int*10, -20.0dB-20.0dB
		35d7	band 2 freq	int, 20Hz-20000Hz
		35d8	band 2 q	int*10, 0.4-9.9
		35d9	band 3 gain	int*10, -20.0dB-20.0dB
		35da	band 3 freq	int, 20Hz-20000Hz
		35db	band 3 q	int*10, 0.4-9.9
		35dc	band 4 gain	int*10, -20.0dB-20.0dB
		35dd	band 4 freq	int, 20Hz-20000Hz
		35de	band 4 q	int*10, 0.4-9.9
		35df	band 5 gain	int*10, -20.0dB-20.0dB
		35e0	band 5 freq	int, 20Hz-20000Hz
		35e1	band 5 q	int*10, 0.4-9.9
		35e2	band 6 gain	int*10, -20.0dB-20.0dB
		35e3	band 6 freq	int, 20Hz-20000Hz
		35e4	band 6 q	int*10, 0.4-9.9
		35e5	band 7 gain	int*10, -20.0dB-20.0dB
		35e6	band 7 freq	int, 20Hz-20000Hz
		35e7	band 7 q	int*10, 0.4-9.9
		35e8	band 8 type	0=peak 1=shelf 2=low pass 3=high pass
		35e9	band 8 gain	int*10, -20.0dB-20.0dB
		35ea	band 8 freq	int, 20Hz-20000Hz
		35eb	band 8 q	int*10, 0.4-9.9
		35ec	band 9 type	0=peak 1=shelf 2=low pass 3=high pass
		35ed	band 9 gain	int*10, -20.0dB-20.0dB
		35ee	band 9 freq	int, 20Hz-20000Hz
		35ef	band 9 q	int*10, 0.4-9.9
	35f0	output 2
		...
	3610	output 3
	3630	output 4
	3650	output 5
	3670	output 6
	3690	output 7
	36b0	output 8
	36d0	output 9
	36f0	output 10
	3710	output 11
	3730	output 12
	3750	output 13
	3770	output 14
	3790	output 15
	37b0	output 16
	37d0	output 17
	37f0	output 18
	3810	output 19
	3830	output 20
3dff	[USB] written with cycling values 0x0000-0x000f, roughly 5 times a second
3e00	cue	0xffff=no cue, [0:7]=cue to, [8:15]=cue from	[W] R?
3e01	?
3e02	control room status	[W]
	[0]=?
	[8]=main mono
	[11]=external input
	[12]=talkback
	[13]=speaker B
	[14]=dim enabled
	[15]=?
3e03	?
3e04	0x67cd	trigger register dump	[W]
3e05	?
3e06	store state	0x0910=slot 1 0x0911=slot 2 0x0912=slot 3 0x0913=slot 4 0x0914=slot 5 0x0915=slot 6	[W]
3e07	?
3e08	time	[0:7]=minute, [8:15]=hour	[W]
3e09	date	[0:4]=day, [5:8]=month, [9:13]=year	[W]
3e9a	durec	[W]
	3e9a	play control	0x8120=stop record, 0x8121=stop, 0x8122=record, 0x8123=play/pause
	3e9b	delete		int, index with high bit set
	3e9c	track select	int
	3e9d	seek		int, seconds
	3e9e	track next/prev	0=previous, 1=next
	3e9f	next track	0xffff=stop, 0x8000=track 1, 0x8001=track 2, ...
	3ea0	play mode	0x8000=single, 0x8001=UFX single, 0x8002=continuous, 0x8003=single next, 0x8004=repeat single, 0x8005=repeat all
3f00	[CC] written with cycling values 0x0000-0x000f, roughly (29.4, 24.3 idle) times a second	[W]
4000	mixer	[W]
	4000	mix 1
		4000	inputs
			4000	input 1 volume
			4001	input 2 volume
			4002	input 3 volume
			4003	input 4 volume
			...
			4013	input 20 volume
		4020	playback
			4020	playback 1 volume
			...
			4033	playback 20 volume
	4040	mix 2
		4040	inputs
		4060	playback
	4080	mix 3
		4080	inputs
		40a0	playback
	40c0	mix 4
	4100	mix 5
	4140	mix 6
	4180	mix 7
	41c0	mix 8
	44c0	mix 20
		44c0	inputs
			44c0	input 1 volume
			...
		44e0	playback
			44e0	playback 1 volume
			...
			44f3	playback 20 volume
47a0	playback fxsend left
	47a0	playback 1
	47a1	playback 2
	...
	47b3	playback 20
47e0	playback fxsend right
	47e0	playback 1
	47e1	playback 2
	...
	47f3	playback 20
```

### Mixer volume formulas

Value format

	[0:14]	signed 15-bit integer, phase-inverted if negative (only on playback channels and playback fxsend)
	[15]	full scale reference, if set, 0x1000, otherwise 0x8000

Decibel to register value

	L = 10^(dB/20)
	value = (L * 0x1000) | 0x8000  // if L > 0.5
	         L * 0x8000            // otherwise

Register value to decibel

	dB = 20*log10((value & 0x7fff) / 0x1000) // if value & 0x8000
	     20*log10(value / 0x8000)            // otherwise

### Panning formulas

Given a peak level `L = 10^(dB/20)`, pan `p` from -100 (left) to
100 (right), and stereo width `w` from -1.0 to 1.0, output levels,
decibels and pan values are calculated as follows:

#### Mono to stereo

The pan law is contant power with sin/cos taper and -3 dB center.

	θ   = (p + 100) * π / 400
	Lₗ  = cos(θ) * L
	Lᵣ  = sin(θ) * L
	dB  = 20 * log10(L)
	pan = p

#### Stereo to stereo

	Lₗₗ  = (100 - max(p, 0)) * (1 + w) / 200 * L
	Lₗᵣ  = (100 + min(p, 0)) * (1 - w) / 200 * L
	Lᵣₗ  = (100 - max(p, 0)) * (1 - w) / 200 * L
	Lᵣᵣ  = (100 + min(p, 0)) * (1 + w) / 200 * L
	Lₗ²  = Lₗₗ² + Lₗᵣ²
	Lᵣ²  = Lᵣₗ² + Lᵣᵣ²
	dBₗ  = 10 * log10(Lₗ²)
	panₗ = acos(2 * Lₗₗ² / Lₗ² - 1) * 200 / π - 100
	dBᵣ  = 10 * log10(Lᵣ²)
	panᵣ = acos(2 * Lᵣₗ² / Lᵣ² - 1) * 200 / π - 100

#### Stereo to mono

	Lₗ   = (100 - max(p, 0)) / 200 * L
	Lᵣ   = (100 + min(p, 0)) / 200 * L
	dBₗ  = 20 * log10(Lₗ)
	panₗ = 0
	dBᵣ  = 20 * log10(Lᵣ)
	panᵣ = 0

### Sample rate enum

	0	32000	[USB-only]
	1	44100
	2	48000
	3	64000	[USB-only]
	4	88200
	5	96000
	6	128000	[USB-only]
	7	176400
	8	192000

## EQ Filters

### Low Pass

$$H(s) = \frac{1}{s^2 + \frac{s}{Q} + 1}$$

$$\left|H\left(i\frac{f}{f_0}\right)\right|^2 = \frac{f_0^4}{(f_0^2 - f^2)^2 + \frac{f_0^2}{Q^2} f^2} = \frac{f_0^4}{f^4 + \left(\frac{1}{Q^2} - 2\right) f_0^2 f^2 + f_0^4}$$

### High Pass

$$H(s) = \frac{s^2}{s^2 + \frac{s}{Q} + 1}$$

$$\left|H\left(i\frac{f}{f_0}\right)\right|^2 = \frac{f^4}{(f_0^2 - f^2)^2 + \frac{f_0^2}{Q^2} f^2} = \frac{f^4}{f^4 + \left(\frac{1}{Q^2} - 2\right) f_0^2 f^2 + f_0^4}$$

### Low Shelf

$$H(s) = A \frac{s^2 + \frac{\sqrt{A}}{Q} s + A}{A s^2 + \frac{\sqrt{A}}{Q} s + 1}$$

$$\left|H\left(i\frac{f}{f_0}\right)\right|^2 = A^2 \frac{(A f_0^2 - f^2)^2 + \frac{A}{Q^2} f_0^2 f^2}{(f_0^2 - A f^2)^2 + \frac{A}{Q^2} f_0^2 f^2} = \frac{f^4 + \left(\frac{1}{Q^2} - 2\right) A f_0^2 f^2 + A^2 f_0^4}{f^4 + \left(\frac{1}{Q^2} - 2\right) \frac{f_0^2}{A} + \frac{f_0^4}{A^2}}$$

### High Shelf

$$H(s) = A \frac{A s^2 + \frac{\sqrt{A}}{Q} s + 1}{s^2 + \frac{\sqrt{A}}{Q} s + A}$$

$$\left|H\left(i\frac{f}{f_0}\right)\right|^2 = A^2 \frac{(f_0^2 - A f^2)^2 + \frac{A}{Q^2} f_0^2 f^2}{(A f_0^2 - f^2)^2 + \frac{A}{Q^2} f_0^2 f^2} = \frac{A^4 f^4 + \left(\frac{1}{Q^2} - 2\right) A^3 f_0^2 f^2 + A^2 f_0^4}{f^4 + \left(\frac{1}{Q^2} - 2\right) A f_0^2 f^2 + A^2 f_0^4}$$

### Peak

$$H(s) = \frac{s^2 + s\frac{A}{Q} + 1}{s^2 + s \frac{1}{AQ} + 1}$$

$$\left|H\left(i\frac{f}{f_0}\right)\right|^2 = \frac{(f_0^2 - f^2)^2 + \frac{A^2 f_0^2}{Q^2} f^2}{(f_0^2 - f^2)^2 + \frac{f_0^2}{A^2 Q^2} f^2} = \frac{f^4 + \left(\frac{A^2}{Q^2} - 2\right) f_0^2 f^2 + f_0^4}{f^4 + \left(\frac{1}{A^2 Q^2} - 2\right) f_0^2 f^2 + f_0^4}$$

### Low Cut (adjustable slope)

> _Thanks to RME for providing information about the low cut filter!_

The low cut filter with adjustable slope is the product of a sequence
of first-order elements, each with its pole scaled by a correction
factor $k_n$ so that the filter's gain at the cut-off frequency
stays at -3 dB. The $n$ th order filter has slope $-6n$ dB/octave.

$$H_n(s) = \left(\frac{s}{s + k_n}\right)^n$$

$$\left|H_n\left(i\frac{f}{f_0}\right)\right|^2 = \left(\frac{f^2}{f^2 + k_n^2 f_0^2}\right)^n$$

To derive the correction factors $k_n$, we want $|H_n(i)^n| = -3
dB = \frac{1}{\sqrt{2}}$. So

```math
\begin{align*}
\frac{1}{\sqrt{2}} &= |H_n(i)^n| = |H_n(i)|^n = \left|\frac{i}{i + k_n}\right|^n = \frac{1}{\sqrt{1 + k_n^2}^n} \\
2 &= (1 + k_n^2)^n \\
\sqrt[n]{2} &= 1 + k_n^2 \\
k_n &= \sqrt{\sqrt[n]{2} - 1}
\end{align*}
```

Plugging in $n = 1, 2, 3, 4$, we get $k_1 = 1$, $k_2 \approx 0.6436$,
$k_3 \approx 0.5098$, $k_4 \approx 0.4350$. However, in practice,
RME uses constants $k_1 = 1$, $k_2 = 0.655$, $k_3 = 0.528$, $k_4 =
0.457$, corresponding to gain $0.7069$, $0.6994$, $0.6910$, and
$0.6836$. I'm not sure how these constants were chosen and why they
differ from the ones I derived, but for consistency, oscmix uses
the RME constants.
