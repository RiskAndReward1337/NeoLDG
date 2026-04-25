# NeoLDG

NeoLDG is a fresh Qt 6 desktop controller for LDG autotuners that expose the
meter/control protocol over a TTL serial connection.

Current model presets:

- LDG-1000ProII
- LDG-600ProII

This project intentionally focuses on the tuner itself:

- serial connection over a USB-to-TTL adapter
- selectable tuner model with model-specific watt calibration
- live forward/reflected power telemetry
- SWR and band display
- memory tune, full tune, bypass, antenna toggle, and auto/manual mode controls
- modern desktop UI with persisted settings

Out of scope for this version:

- amplifier control
- FlexRadio integration
- remote TCP bridge mode

## Why a fresh implementation?

The original `LDGControl` project is a useful reference for command behavior and
workflow, but NeoLDG is implemented from scratch with a new UI and a separate
Qt-based architecture so it can be maintained and published cleanly.

## Build

Requirements:

- CMake 3.21+
- C++20 compiler
- Qt 6 with `Core`, `Widgets`, and `SerialPort`

Build commands:

```bash
cmake -S . -B build
cmake --build build -j
./build/NeoLDG
```

## Releases

GitHub Releases are built automatically from version tags.

- Windows releases are published as a portable `.zip` bundle with the required Qt
  runtime files.
- Linux releases are published as an `AppImage`.

To cut a release from GitHub Actions, push a tag such as `v0.1.0`.

## Hardware Notes

- Use a TTL-level serial adapter, not a full RS-232 port.
- The tuner side expects the LDG mini-DIN wiring used by the original control
  cable.
- Before you use an S-Video Cable, please be sure the two "ground" pins are not tied together, some cheaper cables do this, this will kill your tuner. 

## Protocol Summary

NeoLDG follows the observed LDG control flow used by the original utility:

- send a wake byte (`0x20`, space) before each command
- wait about 1 ms before sending the actual command byte
- use `X` to enter control mode before command/response interactions
- use `S` to resume streaming meter telemetry
- parse meter frames as 6 bytes of payload followed by `;;`

The `LDG-600ProII` preset uses the same control protocol with a reduced power
calibration for watt display.

## Publish Plan

If you split this into its own repository, the cleanest setup is:

1. create a new GitHub repository named `NeoLDG`
2. copy this directory into that repository root
3. add screenshots and release packaging once the first hardware test passes
