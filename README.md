# VIC-20 6502 PRG test port for Adafruit Fruit Jam

This is a public-source candidate for Adafruit Fruit Jam (RP2350B), linked for a resident loader's application partition. It is a **6502/SD/input/video/audio integration test**, not a complete VIC-20 emulator. It does not implement Commodore BASIC/KERNAL/character ROMs, accurate VIC or VIA behavior, keyboard-matrix emulation, cassette/disk support, or general commercial-game compatibility. The `.prg` picker does not make arbitrary VIC-20 programs compatible: the provided sample uses a private-to-this-demo host trap at `$9000`, a screen bridge at `$1E00`, and joystick input at `$9111`.

The previous development-board version passed physical video, left/right input, A-button and frame-rate tests; the user reports that the port's basic testing is complete. The 0.2.0 audio build was compiled, but the preserved project record did not include an independent audio hardware log. This Fruit Jam integration changes the board runtime and has **not** been physically tested. In particular, boot, SD, controls, HDMI video/audio, and frame pacing still need Fruit Jam validation. No binary release is claimed here.

## Try the original demo source

No ROM, `.prg`, save data, or Commodore system image is included. Generate the small original catch-game PRG from its source:

```powershell
python ./scripts/make_test_prg.py
```

Copy `build/test-content/vic20-catch.prg` to `/roms/VIC20/` on the SD card. The picker selects a `.prg` from that directory. Move with left/right; A, B, or X resets the score. On a USB keyboard, arrow keys move, X/Space or Z resets, and C also maps to X. A catch makes a short HDMI tone; resetting the score makes a lower tone. These sounds are demo effects, not VIC-20 sound-chip emulation.

## Build

On Windows, provide Pico SDK 2.3.0 or newer, Pico-PIO-USB, an ARM GCC toolchain, Ninja, and CMake:

```powershell
./scripts/build-release.ps1 -PicoSdkPath C:\path\to\pico-sdk -PicoPioUsbPath C:\path\to\Pico-PIO-USB -ToolchainPath C:\path\to\toolchain -NinjaPath C:\path\to\ninja.exe
```

Optional `-PioasmDir`, `-PicotoolDir`, and `-TinyUsbPath` parameters support prebuilt host tools. The output is `build/fruit-jam-release/picoVic20.uf2`.

The default linker target begins at `0x10080000`, after the resident loader. Do not flash the UF2 as a standalone full-flash image. Check it with:

```powershell
python ./scripts/validate-uf2-layout.py ./build/fruit-jam-release/picoVic20.uf2 --app-base 0x10080000 --metadata 0x10FFFF00
```

## Source and license

The CPU is unmodified `emu6502.cpp`/`.h` from [davervw/c-simple-emu6502-cbm](https://github.com/davervw/c-simple-emu6502-cbm) commit `2c7967752340a2cf3265ad0772e349b60a818f6b` (MIT). The Fruit Jam platform is adapted from the public [PokemonMini port](https://github.com/jackyangantelope/PokemonMini) commit `1212d0d39ed5714ae3816a4046d40e72543f5a83` (BSD-3-Clause). The `app/` frontend and `scripts/make_test_prg.py` are original port code. [THIRD_PARTY.md](THIRD_PARTY.md) details the copied files and component licenses.

The original port code is BSD-3-Clause under [LICENSE](LICENSE). The bundled `pico_shared` subset is GPLv3, so distribution of the combined firmware must also satisfy GPLv3. No Commodore or third-party game data is distributed.
