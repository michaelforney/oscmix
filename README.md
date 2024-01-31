# oscmix

oscmix implements an OSC API for RME's Fireface UCX II running in
class-compliant mode, allowing full control of the device's
functionality through OSC on POSIX operating systems supporting USB
MIDI.

## Current status

Most things work, but still needs a lot more testing, polish,
cleanup. Some things still need to be hooked up in the UI or
implemented in oscmix.

### Supported devices

- RME Fireface UCX II

## Usage

```
oscmix [-dl] [-r recvaddr]... [-s sendaddr]...
```

oscmix reads and writes MIDI SysEx messages from/to file descriptors
6 and 7 respectively, which are expected to be opened.

The `-d` flag enables debug messages.

The `-l` flag enables level meters.

The `-r` flag specifies the address to listen for OSC messages on.

The `-s` flag specifies the address to send OSC messages to.

The `-m` flag is shorthand for `-s udp!224.0.0.1!8222`.

Addresses are specified using the syntax `proto!addr!port`. Currently,
only `udp` is supported. Alternatively, if the address string is a
number, oscmix will read or write to that file descriptor.

By default, oscmix will listen on `udp!127.0.0.1!7222` and send to
`udp!127.0.0.1!8222`. Any addresses specified replace these defaults;
if you want to send to or listen on multiple addresses, pass them
all to `oscmix`.

## Running

### Linux

On Linux systems, you can use bundled `alsarawio` program open and
configure a given raw MIDI subdevice and set up these file descriptors
appropriately.

To determine your MIDI device, look for it in the output of `amidi -l`
(the one ending in `,1` with the name `Fireface UCX II`).

For example:

```sh
alsarawio 2,0,1 oscmix
```

There is also a tool `alsaseqio` that requires alsa-lib and uses
the sequencer API, but it doesn't seem to be as responsive.

To determine the client and port for your device, find it (port 1
of the Fireface UCX II) in the output of `aconnect -l`.

For example:

```sh
alsaseqio 24:1 oscmix
```

### BSD

On BSD systems, you can launch oscmix with file descriptors 6 and
7 redirected to the appropriate MIDI device.

For example:

```sh
oscmix 6<>/dev/rmidi1 7>&6
```

## GTK UI

The [gtk](gtk) directory contains oscmix-gtk, a GTK frontend that
communicates with oscmix using OSC.

![oscmix-gtk](https://mforney.org/misc/oscmix.png)

To run oscmix-gtk without installing, set the `GSETTINGS_SCHEMA_DIR`
environment variable.

```sh
GSETTINGS_SCHEMA_DIR=$PWD/gtk ./gtk/oscmix-gtk
```

## OSC API

The OSC API is not yet final and may change without notice.

| Method | Arguments | Description |
| --- | --- | --- |
| `/input/{1..20}/mute` | `i` enabled | Input *n* muted |
| `/input/{1..20}/fxsend` | `f` db (-65-0) | Input *n* FX send level |
| `/input/{1..20}/stereo` | `i` enabled | Input *n* is stereo |
| `/input/{1..20}/record` | `i` enabled | Input *n* record enabled |
| `/input/{1..20}/planchan` | `i` 0=off 1-60 | Input *n* play channel |
| `/input/{1..20}/msproc` | `i` enabled | Input *n* M/S processing enabled |
| `/input/{1..20}/phase` | `i` enabled | Input *n* phase invert enabled |
| `/input/{1..4}/gain` | `f` 0-75 (n=1,2) 0-24 (n=3,4) | Input *n* gain |
| `/input/{1..2}/48v` | `i` enabled | Input *n* phantom power enabled |
| `/input/{3..8}/reflevel` | `i` 0=+4dBu 1=+13dBu 2=+19dBu | Input *n* reference level |
| `/durec/status` | `i` | DURec status |
| `/refresh` | none | **W** Refresh device registers |
| `/register` | `ii...` register, value | **W** Set device register explicitly |

**TODO** Document rest of API. For now, see the OSC tree in `oscmix.c`.
