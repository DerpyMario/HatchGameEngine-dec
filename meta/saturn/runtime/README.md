# SEGA Saturn runtime for Hatch Game Engine

This directory contains the runtime files needed to build a SEGA Saturn
export from the Hatch Game Engine.

## Files

- `saturn.h` - Header file with Saturn-specific definitions
- `s_main.c` - Main program entry point (to be extended by exporter)
- `string.c` / `string.h` - Basic string functions
- `saturn.ld` - Linker script for Saturn memory layout
- `saturn_start.s` - Startup code and interrupt handlers
- `sat_header.inc` - Cartridge header (overwritten by exporter)

## Building

To build a Saturn export, you need the SEGA Saturn SDK:
https://github.com/SaturnSDK

Set the `SATURN_SDK` environment variable to point to your SDK installation:

```sh
export SATURN_SDK=/path/to/saturn-sdk
cd <export-directory>
make
```

The output will be a `.bin` file that can be run on hardware or emulator.

## License

This runtime is provided as part of the Hatch Game Engine.
