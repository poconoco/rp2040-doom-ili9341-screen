
# RP2040 DOOM (ILI9341 Screen Edition)

**This is a derived work, see original https://github.com/kilograham/rp2040-doom/ and https://github.com/pondahai/rp2040-doom-ili9341 repos it was combined from**

This is a compilation of repos mentioned above along with required submodules and detailed build instructions so it is easy to reproduce the same result.

Instructions below were written and tested on Pop_OS 22.04 LTS, should work for most modern Ubuntu-based distros

# Required hardware

Hardware modules I used to run this configuration of DOOM2:

- RP2040-Plus (the RP2040 board with Type-C connector and 16MB of flash storage).
  This code also should be compatible with standard Pi Pico with 2MB of storage, but you'll have to use the super-tiny mode (see below), and you will be limited to small shareware doom wads. Full DOOM2 requires ~8MB of space, so RP2040-Plus is a perfect fit.
- 320x240 screen with ILI9341 controller. Other controllers can be used, but it would need digging into the source and updating configuration. This repo is preconfigured for ILI9341
- Digital sound module based on MAX98357A. Should also be compatible with PCM5102A
- 8 pushbuttons

Optional if you want it to work without being plugged to a wall
- Li-ion battery
- Switch button to turn power on/off
- RP2040-Plus has onboard charging and power control circuits, so no additional modules needed for this. Bare Pi Pico may require charger module and/or something else.

# Hardware wiring

## Display wiring

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

## Digital sound module wiring

The module I used is MAX98357A, should also be compatible with PCM5102A

| Signal Name | DAC Pin | Pico Pin / GPIO |
| :--- | :--- | :--- |
| **BCLK** | Bit Clock (BCK) | **GPIO 26** |
| **LRCK** | Word Select (WS/LCK) | **GPIO 27** |
| **DIN** | Data In (DATA) | **GPIO 28** |
| **VCC** | Power (VIN) | **3V3 OUT** |
| **GND** | Ground | **Any GND** |

## Push-buttons wiring

Buttons should short the specified GPIO pin to ground when pressed

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

# Build the software

## Set build env

Setup some packages
```
sudo apt update
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential python3 libsdl2-dev libsdl2-mixer-dev pkg-config libsdl2-net-dev libsamplerate0-dev
```

> Some of the packages above are not used in the pico doom build itself (like SDL packages), but are required to run the host build which builds the entire Chocolate Doom, and we need to run that build to build the `whd_gen` tool. It probably can be optimized to not pull those dependencies, but right now I'm leaving it as is.

Checkout the repo and update submodules
```
git clone https://github.com/poconoco/rp2040-doom-ili9341-screen.git ~/rp2040-doom-ili9341-screen
cd ~/rp2040-doom-ili9341-screen
git submodule update --init --recursive
```

## Build pico tools

Build pioasm binary

```
cd ~/rp2040-doom-ili9341-screen
cd pico/pico-sdk/tools/pioasm
mkdir build
cd build
cmake ..
make
```

Build picotool

```
cd ~/rp2040-doom-ili9341-screen
cd pico/picotool
mkdir build
cd build
cmake -DPICO_SDK_PATH=../../pico-sdk ..
make -j12
```

Build the `whd_gen` tool - WAD file converter
```
cd ~/rp2040-doom-ili9341-screen
mkdir build_whd_gen
cd build_whd_gen
cmake ..
make -j12
```

# Convert resources pack

Now you can convert your legal copy of doom.wad or doom2.wad file with `whd_gen` tool
```
cd ~/rp2040-doom-ili9341-screen/build_whd_gen/src/whd_gen
./whd_gen /your/path/to/doom2.wad ~/rp2040-doom-ili9341-screen/doom2.whd -no-super-tiny
```

>**Note:**
>The Doom2 wad requires Pi Pico with 16MB of flash (compressed file is 7.4MB, so may also fit on 8MB, but there should also be place for the code, and I don't have such a board to test on). Also it requires the `-no-super-tiny` flag passed to `whd_gen`. If you are running on a standard 2MB board, you can only use doom1 shareware wad, and DO NOT pass the `-no-super-tiny` flag.

# Build the firmware
```
cd ~/rp2040-doom-ili9341-screen
mkdir build_pico
cd build_pico
cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DPICO_BOARD=pico -DPICO_SDK_PATH=../pico/pico-sdk -DPICO_EXTRAS_PATH=../pico/pico-extras ..
make -j12
```

Check firmware build results (while still in `build_pico` dir)
```
ls src/*.uf2
```
You should see two files ready to go:

- `src/doom_tiny.uf2` (This is the super-tiny version. Use it if you run `whd_gen` WITHOUT `-no-super-tiny` flag)
- `src/doom_tiny_nost.uf2` (This is a not super-tiny version. Use it if you run `whd_gen` WITH `-no-super-tiny` flag)

# Flashing the code and resources pack

There are two operations:
- flash the resources pack using the `picotool` we built
- copy the uf2 file using the file manager


To enter flashing mode, hold the BOOTSEL (or just BOOT) button on your Pico and plug it into your USB port.
Then flash the whd file we generated earlier using `picotool`:
```
cd ~/rp2040-doom-ili9341-screen
sudo pico/picotool/build/picotool load -v -t bin doom2.whd -o 0x10048000
```

The `0x10048000` is the special offset, the doom code will look for resources at this offset

Now, when pico is still connected and mounted as a flash drive, use your file manager to copy the `src/doom_tiny_nost.uf2` or `src/doom_tiny.uf2` (depending on if you are using super-tiny whd or not, see above) file to the root of that drive.

When copying finishes, Pico should reboot automatically. And if everything was wired, built and flashed correctly, DOOM should run.
