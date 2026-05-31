
# What is this

This is a combined sources from repos kilograham/rp2040-doom/ and pondahai/rp2040-doom-ili9341 to provide ready to build and use
version of Doom port to RP2040 with ILI9341 screen.

Instructions below were writtend and tested on Pop_OS 22.04 LTS, should work for most modern ubuntu-based distros

# Hardware

What I used to run DOOM2:

- RP2040-Plus (the board with Type-C connector and 16Mb or flash storage). But it also should be compatible with standard Pi Pico with 2Mb of storage, but you'll have to use the super-tiny mode (see below), and you will be limited to what WADs you will be able to load onto the board. DOOM2 requires ~8 Mb of space, so RP-2040 is a perfect fit.
- 320x240 screen with ILI9341 controller. Other controllers can be used, but it would need digging into the source and updating configuration. This repo is preconfigured for ILI9341
- Digital sound module based on MAX98357A should also be compatible with PCM5102A
- 8 pushbuttons


# Instructions to build

## Set build env

Setup some packages
```
sudo apt update
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential python3 libsdl2-dev libsdl2-mixer-dev pkg-config libsdl2-net-dev libsamplerate0-dev
```

** Some of the packages above are not used in the pico doom build itself (like SDL packages), but are required to run the host build which builds the entire ChocolateDoom, and we need to run that build to build the whd_gen tool. It probably can be optimized to not pull those dependencies, but right now I'm leaving it as is.

Checkout the repo and update submodules
```
git clone https://github.com/poconoco/rp2040-doom-ili9341-screen.git ~/rp2040-doom-ili9341-screen
cd ~/rp2040-doom-ili9341-screen
git submodule update --init --recursive
```

## Build pico tools

### Build pioasm binary

```
cd ~/pico-sdk/tools/pioasm
mkdir build
cd build
cmake ..
make
```

1. Clone the official picotool repository to your home folder
```
git clone https://github.com/raspberrypi/picotool.git ~/picotool
cd ~/picotool
git fetch --all --tags
git checkout 1.1.2
```

2. Set up the build environment
```
mkdir build
cd build
```

3. Configure it using your existing Pico SDK
```
cmake -DPICO_SDK_PATH=~/pico-sdk ..
```

4. Compile it (using your 12 CPU threads!)
```
make -j12
```

5. Install it globally so you can run the command from any folder
```
sudo make install
```

## Build the `whd_gen` tool - WAD file converter
```
cd /path/to/your/git/rp2040-doom-ili9341-screen
git submodule update --init --recursive
mkdir build
cd build
cmake ..
make -j8
```

Check `whd_gen` util build results (while still in `build` dir)
```
cd src/whd_gen
ls whd_gen
```

You should see the `whd_gen` util binary built.

# Convert resources pack

Now you can convert your legal copy of doom.wad or doom2.wad file with it:
```
cd /path/to/your/git/rp2040-doom-ili9341-screen/build/src/whd_gen
./whd_gen /your/path/to/doom2.wad doom2.whd -no-super-tiny
```

The `-no-super-tiny` option disables agressive compression to fin into the 2Mb of regular Pi Pico board, because I use RP2040-Plus board with 16Mb or flash storage. So am pushing the full doom2.wad. To work with 2Mb board, use shareware doom.wad file and compress it without the `-no-super-tiny` option.

# Build the firmware
```
cd /your/path/to/rp2040-doom-ili9341-screen
mkdir build_pico
cd build_pico
cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DPICO_BOARD=pico -DPICO_SDK_PATH=../pico/pico-sdk -DPICO_EXTRAS_PATH=../pico/pico-extras ..
```

Check firmware build results (while still in `build_pico` dir)
```
ls src/*.uf2
```
You should see two files ready to go:

- `src/doom_tiny.uf2` (This is the super-tiny version. Use it if you run `whd_gen` without `-no-super-tiny` flag)
- `src/doom_tiny_nost.uf2` (This is a not super-tiny version. Use it if you run `whd_gen` with `-no-super-tiny` flag)

# Flashing the code and resources pack

1. Hold the BOOTSEL button on your Pico and plug it into your USB port.
2. In terminal, navigate to where you generated the *.hhd file (I'll be using doom2.whd)
3. Run the picotool command to write the *.whd data to the specific offset:
```
cd /path/to/your/git/rp2040-doom-ili9341-screen/build/src/whd_gen
sudo picotool load -v -t bin doom2.whd -o 0x10048000
```
4. Now, when pico is still connected and mounted as a flash drive, copy to the root of that drive the `doom_tiny.uf2` file we build earlier.
5. When finish copying, Pico should reboot automatically


# Hardware wiring

## Wire the display

For an ILI9341 SPI display, connect:

| Signal Name | Display Pin | Pico Pin / GPIO |
| :--- | :--- | :--- |
| **SCLK** | SPI Clock | **GPIO 18** |
| **MOSI** | Data In | **GPIO 19** |
| **CS** | Chip Select | **GPIO 17** |
| **DC** | Data/Command | **GPIO 20** |
| **RESET** | Reset | **GPIO 21** |
| **BL** | Backlight | **GPIO 22** |
| **MISO** | Data Out | Not Used |
| **VCC** | Power (VIN) | **3V3 OUT** |
| **GND** | Ground | **Any GND** |

At this point you should be able to plug in the USB-C power and if everything went OK, the DOOM should launch and start playing demos.

## Wire the digital sound module

The module I used is MAX98357A, should also be compatible with PCM5102A

| Signal Name | DAC Pin | Pico Pin / GPIO |
| :--- | :--- | :--- |
| **BCLK** | Bit Clock (BCK) | **GPIO 26** |
| **LRCK** | Word Select (WS/LCK) | **GPIO 27** |
| **DIN** | Data In (DATA) | **GPIO 28** |
| **VCC** | Power (VIN) | **3V3 OUT** |
| **GND** | Ground | **Any GND** |

## Wire push-buttons

Buttons should short the specified GPIO pin to gground when pressed
Pinouit:

| Define Button | GPIO | Function in DOOM |
| :--- | :--- | :--- |
| **PIN_UP** | 9 | Move Forward |
| **PIN_DN** | 5 | Move Backward |
| **PIN_LT** | 8 | Turn Left |
| **PIN_RT** | 6 | Turn Right |
| **PIN_A** | 2 | **Fire** (Space + Left Shift) |
| **PIN_B** | 3 | **Open Door / Use** (Right Ctrl) |
| **PIN_ST** | 4 | **Menu / Enter** (Enter + Tab) |
| **PIN_SL** | 0 | **Escape** (Pause / Menu) |


# ============================================= #
# Original README of pondahai/rp2040-doom-ili9341
# ============================================= #

改編自 https://github.com/kilograham/rp2040-doom

感謝kilograham做了絕大部分的貢獻 他把doom源碼做了很大的改寫 讓doom可以在rp2040(樹莓派pico)上運行 我只是修改成可以在LCD(ILI9341)上顯示

我在這裡只有放我改過的源碼 並且在修改的地方插入//dahai這樣的記號 主要的修改邏輯是 1.關掉用不到或是衝突的GPIO 2.將原本的scanvideo輸出關閉，改成將緩衝區資料送到LCD

Adapted from https://github.com/kilograham/rp2040-doom Thanks to kilograham for making most of his contributions. He made a big rewrite of the doom source code. Let doom run on rp2040 (Raspberry Pi pico)

I just modified it so that it can be displayed on the LCD (ILI9341). I only put the source code I changed here. And insert a mark like //dahai in the modified place. The main modification logic is

Turn off the useless or conflicting GPIO
Turn off the original scanvideo output and send the buffer data to the LCD instead.


# ============================================= #
# Original README of kilograham/rp2040-doom/
# ============================================= #

# RP2040 (+RP2350) Doom

This is a port of Doom for RP2040 / RP2350 devices, derived from [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom).

Significant changes have been made to support running on the RP2xxx device, but particularly to support running the 
entire shareware `DOOM1.WAD` which is 4M big on a Raspberry Pi Pico with only 2M flash!

You can read many details on this port in the blog post [here](https://kilograham.github.io/rp2040-doom/).

Note that a hopefully-fully-functional `chocolate-doom` executable is buildable from this RP2xxx code base as a 
means of 
verification that everything still works, but whilst they can still be built, Hexen, Strife and Heretic are almost 
certainly broken, so are not built by default.

This chocolate-doom commit that the code is branched off can be found in the `upstream` branch.

The original Chocolate Doom README is [here](README-chocolate.md).

## Code State

Thus far, the focus has been entirely on getting RP2040 Doom running. Not a lot of time has been 
spent 
cleaning 
the code up. There are a bunch of defunct `#ifdefs` and other code that was useful at some point, 
but no longer are, and indeed changing them may result in non-functional code. This is particularly 
true of 
the 
`whd_gen` tool 
used to 
convert/compress WADs 
who's code is 
likely completely incomprehensible!  

## Artifacts

You can find a RP2040 Doom UF2s based on the standard VGA/I2S pins in the 
releases of this repository. There are also versions with the shareware DOOM1.WAD already embedded.

Note you can always use `picotool info -a <UF2 file>` to see the pins used by a particular build.

## Goals

The main goals for this port were:

1. Everything should match the original game experience, i.e. all the graphics at classic 320x200 resolution, stereo
   sound,
   OPL2 music, save/load, demo playback, cheats, network multiplayer... basically it should feel like the original game.
2. `DOOM1.WAD` should run on a Raspberry Pi Pico. There was also to be no sneaky discarding of splash screens, altering of levels, down-sampling of
   textures or whatever. RP2040 boards with 8M should be able to play at least the full *Ultimate Doom* and *DOOM II*
   WADs.
3. The RP2040 should output directly to VGA (16 color pins for RGB565 along with HSync/VSync) along with stereo sound.

## Results

[![RP2040 Doom on a Raspberry Pi Pico](https://img.youtube.com/vi/eDVazQVycP4/maxresdefault.jpg)](https://youtu.be/eDVazQVycP4)

Features:

* Full `DOOM1.WAD` playable on Raspberry Pi Pico with 2M flash.
* *Ultimate Doom* and *Doom II* are playable on 8M devices.
* 320x200x60 VGA output (really 1280x1024x60).
* 9 Channel OPL2 Sound at 49716Hz.
* 9 Channel Stereo Sound Effects.
* I2C networking for up to 4 players.
* Save/Load of games.
* All cheats supported.
* Demos from original WADs run correctly.
* USB Keyboard Input support.
* All end scenes, intermissions, help screens etc. supported.
* Good frame rate; generally 30-35+ FPS.
* Uses 270Mhz overclock (requires flash chip that will run at 135Mhz)

# Building

RP2040 Doom should build fine on Linux and macOS. The RP2040 targeting builds should also work on Windows, though I 
haven't tried.

The build uses `CMake`.

## Regular chocolate-doom/native builds

To build everything, assuming you have SDL2 dependencies installed, you can create a build directory:

```bash
mkdir build
cd build
cmake ..
```

And then run `make` or `make -j<num_cpus>` from that directory. To build a particular target e.g. `chocolate-doom`, 
do `make chocolate-doom`

Note this is the way you build the `whd_gen` tool too.

## RP2040 Doom builds

You must have [pico-sdk](https://github.com/raspberrypi/pico-sdk) and 
**the latest version of** [pico-extras](https://github.com/raspberrypi/pico-extras) installed, along with the regular 
pico-sdk requisites (e.g.
`arm-none-eabi-gcc`). If in doubt, see the Raspberry Pi
[documentation](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). I have been building against 
the `develop` branch of `pico-sdk`, so I recommend that..

**NOTE: I am building with arm-none-eabi-gcc 13.2.rel1 .. whilst other versions may work, changes in compiler version may affect the binary size which, being tight, can cause problems (either link failure, or you may see stack overflow in the form of color palette corruption). Particularly I know tjhat arm-none-eabi-gcc 10.x versions don't work well.**

For USB keyboard input support, RP2040 Doom currently uses a modified version of TinyUSB included as a submodule. 
Make sure you have initialized this submodule via `git submodule update --init` 

You can create a build directly like this:

```bash
mkdir rp2040-build
cd rp2040-build
cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DPICO_BOARD=vgaboard -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_EXTRAS_PATH=/path/to/pico-extras ..
```

Note that the `PICO_BOARD` setting is for the standard VGA demo board which has RGB on pins 0->15, sync pins on 16,17 
and 
I2S on 26,27,28.

As before, use `make` or `make <target>` to build. 

The RP2040 version has four targets, each of which create a similarly named `UF2` file (e.g. `doom_tiny.uf2`). 
These UF2 files contain the executable code/data, but they do not contain the WAD data which is converted into a 
RP2040 Domom 
specific WHD/WHX format by `whd_gen` (for more see below). The WHD/WHX file must also be loaded onto the device at a 
specific address which varies by binary. 

"super-tiny" refers to RP2040 Doom builds that use the more compressed WHX format, and 
required for`DOOM1.
WAD` to 
run 
on a 2M Raspberry Pi Pico. "Non super-tiny" refers to RP2040 Doom builds that use the WHD format which is larger, but 
also is 
required for *Ultimate Doom* and *Doom II* WADs. These binaries are distinct as supporting both formats in the same 
binary would just have made things bigger and slower.


* **doom_tiny** This is a "super tiny" version with no USB keyboard support. You can use
[SDL Event Forwarder](https://github.com/kilograham/sdl_event_forwarder) to tunnel keyboard input from your host 
  computer over UART. The WHX file must be loaded at `0x10040000`. 
* **doom_tiny_usb** This is a "super tiny" version with additional USB keyboard support. Because of the extra USB 
  code, the WHX file must be loaded at `0x10042000`. As you can see USB support via TinyUSB causes the binary to 
  grow by 2K (hence the move of the WHX file address) leaving less space for saved games (which are also stored in 
  flash).
* **doom_tiny_nost** This is a "non super tiny" version of `doom_tiny` supporting larger WADs stored as WHD. The WHD 
  file must be loaded at `0x10048000`
* **doom_tiny_nost_usb** This is a "non super tiny" version of `doom_tiny_usb` supporting larger WADs stored as 
  WHD. The WHD
  file must be loaded at `0x10048000`

You can load you WHD/WHX file using [picotool](https://github.com/raspberrypi/picotool). e.g.

```bash
picotool load -v -t bin doom1.whx -o 0x10042000.
```

See `whd_gen` further below for generating `WHX` or `WHD` files.

#### USB keyboard support

Note that TinyUSB host mode support for keyboard may not work with all keyboards especially since the RP2040 Doom 
has been built with small limits for number/sizes of hubs etc. I know that Raspberry Pi keyboards work fine, as 
did my ancient 
Dell keyboard. Your keyboard may just do nothing, or may cause a crash. If so, for now, you are stuck forwarding 
keys from another PC via sdl_event_forwarder.

### RP2040 Doom builds not targeting an RP2040 device

You can also build the RP2040 Doom to run on your host computer (Linux or macOS) by using
[pico_host_sdl](https://github.com/raspberrypi/pico-host-sdl) which simulates RP2040 based video/audio output using SDL.

This version currently embeds the WHD/WHX in `src/tiny.whd.h` so you must generate this file.

You can do this via `./cup.sh <whd/whx_file>`

```bash
mkdir host-build
cd host-build
cmake -DPICO_PLATFORM=host -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_EXTRAS_PATH=/path/to/pico-extras -DPICO_SDK_PRE_LIST_DIRS=/path/to/pico_host_sdl ..
```

... and then `make` as usual.

## whd_gen

`doom1.whx` is includd in this repository, otherwise you need to build `whd_gen` using the regular native build 
instructions above.

To generate a WHX file (you must use this to convert DOOM1.WAD to run on a 2M Raspberry Pi Pico)

```bash
whd_gen <wad_file> <whx_file>
```

The larger WADs (e.g. *Ultimate Doom* or *Doom II* have levels which are too complex to convert into a super tiny 
WHX file. These larger WADs are not going to fit in a 2M flash anywy, so the less compressed WHD format can be used 
given that the device now probably has 8M of flash.

```bash
whd_gen <wad_file> <whd_file> -no-super-tiny
```

Note that `whd_gen` has not been tested with a wide variety of WADs, so whilst it is possible that non Id WADs may 
work, it is by no means guaranteed!

NOTE: You should use a release build of `whd_gen` for the best sound effect fidelity, as the debug build 
deliberately lowers the encoding quality for the sake of speed.

# Running the RP2040 version

The releases here use pins as defined when building with `PICO_BOARD=vgaboard`:

```
 0-4:    Red 0-4
 6-10:   Green 0-4
 11-15:  Blue 0-4
 16:     HSync
 17:     VSync
 18:     I2C1 SDA
 19:     I2C1 SCL
 20:     UART1 TX
 21:     UART1 RX
 26:     I2S DIN
 27:     I2S BCK
 28:     I2S LRCK
```
You can always find these from your ELF or UF2 with 

```
picotool info -a <filename>
``` 

These match for example the Pimoroni Pico VGA Demo Base which itself is based on the suggested 
Raspberry Pi Documentation [here](https://datasheets.raspberrypi.com/rp2040/hardware-design-with-rp2040.pdf)
and the design files zipped [here](https://datasheets.raspberrypi.com/rp2040/VGA-KiCAD.zip).

# Future

*Evilution* and *Plutonia* are not yet supported. There is an issue tracking it 
[here](https://github.com/kilograham/rp2040-doom/issues/1).

# RP2040 Doom Licenses

* Any code derived from chocolate-doom matinains its existing license (generally GPLv2). 
* New RP2040 Doom specific code not implementing existing chocolate-doom interfaces is licensed BSD-3.
* ADPCM-XA is unmodified and is licensed BSD-3.
* Modified emu8950 derived code retains its MIT license.

