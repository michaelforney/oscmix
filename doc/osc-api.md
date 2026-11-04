# OSC API

The OSC API is not yet final and may change without notice.

**R** = read-only (sent from device to client). **W** = write-only (no response from device). Unmarked = read/write.

Enum values are sent as `is` (integer index + string name) and accepted as either `i` (index) or `s` (name string).

### Inputs (`/input/{1..20}/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/input/{1..20}/mute` | `i` enabled | Mute |
| `/input/{1..20}/fx` | `f` dB (-65 to 0) | FX send level |
| `/input/{1..20}/stereo` | `i` enabled | Stereo pair with next channel |
| `/input/{1..20}/record` | `i` enabled | Record enable |
| `/input/{1..20}/name` | `s` name | **W** Channel name (up to 11 chars) |
| `/input/{1..20}/playchan` | `i` 1-60, 0=off | Playback channel assignment |
| `/input/{1..20}/msproc` | `i` enabled | M/S processing |
| `/input/{1..20}/phase` | `i` enabled | Phase invert |
| `/input/{1..2}/gain` | `f` 0-75.0 (dB) | Mic/Line gain |
| `/input/{3..4}/gain` | `f` 0-24.0 (dB) | Inst/Line gain |
| `/input/{1..2}/48v` | `i` enabled | Phantom power |
| `/input/{1..4}/autoset` | `i` enabled | Auto-set gain |
| `/input/{3..4}/hi-z` | `i` enabled | Hi-Z instrument input |
| `/input/{3..8}/reflevel` | `is` +13dBu, +19dBu | Reference level |
| `/input/{1..20}/lowcut` | `i` enabled | Low-cut filter enable |
| `/input/{1..20}/lowcut/freq` | `i` 20-500 (Hz) | Low-cut frequency |
| `/input/{1..20}/lowcut/slope` | `i` | Low-cut slope |
| `/input/{1..20}/eq` | `i` enabled | EQ enable |
| `/input/{1..20}/eq/band1freq` | `i` 20-20000 (Hz) | EQ band 1 frequency |
| `/input/{1..20}/eq/band1gain` | `f` -20.0 to 20.0 (dB) | EQ band 1 gain |
| `/input/{1..20}/eq/band1q` | `f` 0.4-9.9 | EQ band 1 Q |
| `/input/{1..20}/eq/band1type` | `is` Peak, Low Shelf, High Pass, Low Pass | EQ band 1 type |
| `/input/{1..20}/eq/band2freq` | `i` 20-20000 (Hz) | EQ band 2 frequency |
| `/input/{1..20}/eq/band2gain` | `f` -20.0 to 20.0 (dB) | EQ band 2 gain |
| `/input/{1..20}/eq/band2q` | `f` 0.4-9.9 | EQ band 2 Q |
| `/input/{1..20}/eq/band3freq` | `i` 20-20000 (Hz) | EQ band 3 frequency |
| `/input/{1..20}/eq/band3gain` | `f` -20.0 to 20.0 (dB) | EQ band 3 gain |
| `/input/{1..20}/eq/band3q` | `f` 0.4-9.9 | EQ band 3 Q |
| `/input/{1..20}/eq/band3type` | `is` Peak, High Shelf, Low Pass, High Pass | EQ band 3 type |
| `/input/{1..20}/dynamics` | `i` enabled | Dynamics enable |
| `/input/{1..20}/dynamics/gain` | `f` -30.0 to 30.0 (dB) | Dynamics gain |
| `/input/{1..20}/dynamics/attack` | `i` 0-200 (ms) | Attack time |
| `/input/{1..20}/dynamics/release` | `i` 100-999 (ms) | Release time |
| `/input/{1..20}/dynamics/compthres` | `f` -60.0 to 0.0 (dB) | Compressor threshold |
| `/input/{1..20}/dynamics/compratio` | `f` 1.0-10.0 | Compressor ratio |
| `/input/{1..20}/dynamics/expthres` | `f` -99.0 to 20.0 (dB) | Expander threshold |
| `/input/{1..20}/dynamics/expratio` | `f` 1.0-10.0 | Expander ratio |
| `/input/{1..20}/dynamics/meter` | `i` | **R** Dynamics gain reduction meter |
| `/input/{1..20}/autolevel` | `i` enabled | Auto-level enable |
| `/input/{1..20}/autolevel/maxgain` | `f` 0.0-18.0 (dB) | Auto-level max gain |
| `/input/{1..20}/autolevel/headroom` | `f` 3.0-12.0 (dB) | Auto-level headroom |
| `/input/{1..20}/autolevel/risetime` | `f` 0.1-9.9 (s) | Auto-level rise time |
| `/input/{1..20}/autolevel/meter` | `i` | **R** Auto-level meter |

### Outputs (`/output/{1..20}/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/output/{1..20}/volume` | `f` -65.0 to 6.0 (dB) | Output volume |
| `/output/{1..20}/pan` | `i` -100 to 100 | Pan |
| `/output/{1..20}/mute` | `i` enabled | Mute |
| `/output/{1..20}/fx` | `f` -65.0 to 0.0 (dB) | FX return level |
| `/output/{1..20}/stereo` | `i` enabled | Stereo pair with next channel |
| `/output/{1..20}/record` | `i` enabled | Record enable |
| `/output/{1..20}/name` | `s` name | **W** Channel name (up to 11 chars) |
| `/output/{1..20}/playchan` | `i` | Playback channel assignment |
| `/output/{1..20}/phase` | `i` enabled | Phase invert |
| `/output/{1..6}/reflevel` | `is` +4dBu, +13dBu, +19dBu | Analog output reference level |
| `/output/{7..8}/reflevel` | `is` Low, High | Phones output reference level |
| `/output/{1..20}/crossfeed` | `i` | Crossfeed |
| `/output/{1..20}/volumecal` | `f` -24.00 to 3.00 (dB) | Volume calibration |
| `/output/{1..20}/loopback` | `i` enabled | **W** Loopback |
| `/output/{1..20}/lowcut` | `i` enabled | Low-cut filter enable (same subtree as input) |
| `/output/{1..20}/eq` | `i` enabled | EQ enable (same subtree as input) |
| `/output/{1..20}/dynamics` | `i` enabled | Dynamics enable (same subtree as input) |
| `/output/{1..20}/autolevel` | `i` enabled | Auto-level enable (same subtree as input) |
| `/output/{1..20}/roomeq` | `i` enabled | Room EQ enable |
| `/output/{1..20}/roomeq/delay` | `f` 0-0.425 (s) | Room EQ delay |
| `/output/{1..20}/roomeq/band1type` | `is` Peak, Low Shelf, High Pass, Low Pass | Room EQ band 1 type |
| `/output/{1..20}/roomeq/band{1..9}freq` | `i` 20-20000 (Hz) | Room EQ band frequency |
| `/output/{1..20}/roomeq/band{1..9}gain` | `f` -20.0 to 20.0 (dB) | Room EQ band gain |
| `/output/{1..20}/roomeq/band{1..9}q` | `f` 0.4-9.9 | Room EQ band Q |
| `/output/{1..20}/roomeq/band8type` | `is` Peak, High Shelf, Low Pass, High Pass | Room EQ band 8 type |
| `/output/{1..20}/roomeq/band9type` | `is` Peak, High Shelf, Low Pass, High Pass | Room EQ band 9 type |

### Playback channels (`/playback/{1..20}/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/playback/{1..20}/mute` | `i` enabled | **W** Mute |
| `/playback/{1..20}/stereo` | `i` enabled | **W** Stereo pair with next channel |

### Mix matrix (`/mix/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/mix/{1..20}/input/{1..20}` | `f` dB, `i` pan (-100 to 100) | Mix level and pan for input *n* into output *out* |
| `/mix/{1..20}/playback/{1..20}` | `f` dB, `i` pan (-100 to 100) | Mix level and pan for playback *n* into output *out* |

Volume argument may be `N` (nil) to update pan only without changing volume.

### Reverb (`/reverb/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/reverb` | `i` enabled | Reverb enable |
| `/reverb/type` | `is` Small Room, Medium Room, Large Room, Walls, Shorty, Attack, Swagger, Old School, Echoistic, 8plus9, Grand Wide, Thicker, Envelope, Gated, Space | Reverb type |
| `/reverb/predelay` | `i` (ms) | Pre-delay |
| `/reverb/lowcut` | `i` (Hz) | Low-cut frequency |
| `/reverb/roomscale` | `f` | Room scale |
| `/reverb/attack` | `i` | Attack |
| `/reverb/hold` | `i` | Hold |
| `/reverb/release` | `i` | Release |
| `/reverb/highcut` | `i` (Hz) | High-cut frequency |
| `/reverb/time` | `f` (s) | Reverb time |
| `/reverb/highdamp` | `i` | High damping |
| `/reverb/smooth` | `i` | Smooth |
| `/reverb/volume` | `f` (dB) | Reverb volume |
| `/reverb/width` | `f` | Stereo width |

### Echo (`/echo/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/echo` | `i` enabled | Echo enable |
| `/echo/type` | `is` Stereo Echo, Stereo Cross, Pong Echo | Echo type |
| `/echo/delay` | `f` 0-2.0 (s) | Delay time |
| `/echo/feedback` | `i` | Feedback |
| `/echo/highcut` | `is` Off, 16kHz, 12kHz, 8kHz, 4kHz, 2kHz | High-cut frequency |
| `/echo/volume` | `f` -65.0 to 6.0 (dB) | Echo volume |
| `/echo/width` | `f` | Stereo width |

### Control room (`/controlroom/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/controlroom/mainout` | `is` 1/2, 3/4, 5/6, 7/8, 9/10, 11/12, 13/14, 15/16, 17/18, 19/20 | Main output pair |
| `/controlroom/mainmono` | `i` enabled | Main mono |
| `/controlroom/muteenable` | `i` enabled | Mute enable |
| `/controlroom/dimreduction` | `f` -65.0 to 0.0 (dB) | Dim reduction level |
| `/controlroom/dim` | `i` enabled | Dim |
| `/controlroom/recallvolume` | `f` -65.0 to 0.0 (dB) | Recall volume |

### Clock (`/clock/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/clock/source` | `is` Internal, Word Clock, SPDIF, AES, Optical | Clock source |
| `/clock/samplerate` | `i` (Hz) | **R** Current sample rate |
| `/clock/wckout` | `i` enabled | Word clock output |
| `/clock/wcksingle` | `i` enabled | Word clock single speed |
| `/clock/wckterm` | `i` enabled | Word clock termination |

### Hardware (`/hardware/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/hardware/opticalout` | `is` ADAT, SPDIF | Optical output format |
| `/hardware/spdifout` | `is` Consumer, Professional | S/PDIF output format |
| `/hardware/ccmode` | `i` | **R** Class-compliant mode |
| `/hardware/ccmix` | `is` TotalMix App, 6ch + phones, 8ch, 20ch | Class-compliant mix mode |
| `/hardware/standalonemidi` | `i` enabled | Standalone MIDI control |
| `/hardware/standalonearc` | `is` Volume, 1s Op, Normal | Standalone ARC mode |
| `/hardware/lockkeys` | `is` Off, Keys, All | Lock keys |
| `/hardware/remapkeys` | `i` enabled | Remap keys |
| `/hardware/eqdrecord` | `i` | **W** EQ+D record |
| `/hardware/dspload` | `i` (%) | **R** DSP load |
| `/hardware/dspvers` | `i` | **R** DSP version |

### DURec (`/durec/...`)

| Method | Arguments | Description |
| --- | --- | --- |
| `/durec/play` | none | **W** Start playback |
| `/durec/stop` | none | **W** Stop |
| `/durec/record` | none | **W** Start recording |
| `/durec/delete` | `i` file index | **W** Delete file |
| `/durec/file` | `i` file index | Select file |
| `/durec/status` | `is` No Media, Filesystem Error, Initializing, Reinitializing, Stopped, Recording, Playing, Paused | **R** DURec status |
| `/durec/position` | `i` 0-100 (%) | **R** Playback position |
| `/durec/time` | `i` | **R** Playback time |
| `/durec/usbload` | `i` | **R** USB load |
| `/durec/usberrors` | `i` | **R** USB errors |
| `/durec/totalspace` | `f` (GB) | **R** Total storage space |
| `/durec/freespace` | `f` (GB) | **R** Free storage space |
| `/durec/numfiles` | `i` | **R** Number of files |
| `/durec/next` | `i` | **R** Next file index |
| `/durec/playmode` | `is` Single, UFX Single, Continuous, Single Next, Repeat Single, Repeat All | **R** Play mode |
| `/durec/recordtime` | `i` | **R** Remaining record time |
| `/durec/name` | `is` index, name | **R** File name |
| `/durec/samplerate` | `ii` index, Hz | **R** File sample rate |
| `/durec/channels` | `ii` index, count | **R** File channel count |
| `/durec/length` | `ii` index, length | **R** File length |

### Miscellaneous

| Method | Arguments | Description |
| --- | --- | --- |
| `/refresh` | none | **W** Refresh all device registers |
