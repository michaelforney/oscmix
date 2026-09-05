# oscmix

[![builds.sr.ht status](https://builds.sr.ht/~mcf/oscmix/commits/main.svg)](https://builds.sr.ht/~mcf/oscmix/commits/main)

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

## Build prerequisites

Debian/Ubuntu:
```sh
apt install build-essential pkg-config libasound2-dev libgtk-3-dev clang lld wasi-libc
```

Fedora:
```sh
dnf install gcc make pkgconf alsa-lib-devel gtk3-devel clang lld wasi-libc
```

Arch:
```sh
pacman -S base-devel alsa-lib gtk3 clang lld wasi-libc
```

## Usage

```
oscmix [-dlm] [-r recvaddr] [-s sendaddr]
```

oscmix reads and writes MIDI SysEx messages from/to file descriptors
6 and 7 respectively, which are expected to be opened.

By default, oscmix will listen for OSC messages on `udp!127.0.0.1!7222`
and send to `udp!127.0.0.1!8222`.

See the manual, [oscmix.1], for more information.

[oscmix.1]: https://michaelforney.github.io/oscmix/oscmix.1.html

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
the sequencer API.

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

## Web UI

The [web](web) directory contains a web frontend that can communicate
with oscmix through OSC over a WebSocket, or by directly to an
instance of oscmix compiled as WebAssembly running directly in the browser.

![oscmix-web]

The web UI is automatically deployed at
[https://michaelforney.github.io/oscmix](https://michaelforney.github.io/oscmix).

It is tested primarily against the chromium stable channel, but
patches to support other/older browsers are welcome (if it doesn't
complicate things too much).

Also included is a UDP-to-WebSocket bridge, `wsdgram`. It expects
file descriptors 0 and 1 to be an open connection to a WebSocket
client. It forwards incoming messages to a UDP address and writes
outgoing messages for any UDP packet received. Use it in combination
with software like [s6-tcpserver] or [s6-tlsserver].

```sh
s6-tcpserver 127.0.0.1 8222 wsdgram
```

To build `oscmix.wasm`, you need `clang` supporting wasm32, `wasm-ld`,
and `wasi-libc`.

[oscmix-web]: https://github.com/michaelforney/oscmix/assets/52851/ef22e75e-9d38-4c82-b016-81bce77be571
[s6-tcpserver]: https://skarnet.org/software/s6-networking/s6-tcpserver.html
[s6-tlsserver]: https://skarnet.org/software/s6-networking/s6-tlsserver.html

## OSC API

The OSC API is not yet final and may change without notice.

See [doc/osc-api.md] for the full API reference.

[doc/osc-api.md]: doc/osc-api.md

## Contact

There is an IRC channel #oscmix on irc.libera.chat.
