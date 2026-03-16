# AGENTS.md

This repository keeps only:

- `wsh264/` and the minimal websocket/crypto support it needs
- `gateway/` host relay
- `gateway_client/` host playback client
- `3rd_party/chacha` shared third-party dependency
- AmebaPro firmware build scripts and burn tooling

Legacy `tinyhttpd`, `genbin`, `htdocs`, `ministd`, and `ws_server` sources have been removed.

## Init

```bash
git submodule update --init --recursive
```

## Build Commands

### Build `wsh264`
```bash
cmake -S . -B build
cmake --build build --target wsh264
```

### Build `gateway`
```bash
cmake -S . -B build
cmake --build build --target gateway
```

### Build `gateway_client`
```bash
cmake -S . -B build
cmake --build build --target gateway_client
```

### Build `wsh264` Standalone
```bash
cmake -S wsh264 -B wsh264/build
cmake --build wsh264/build --target wsh264
```

### Build Ameba Firmware on Linux
```bash
cd ./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE
make clean
make
```

### Build Ameba Firmware on macOS
```bash
./scripts/build_ameba_firmware.sh --macos-host-toolchain
```

## Key Development Notes

- The actively maintained host binaries are `wsh264`, `gateway`, and `gateway_client`
- `wsh264` derives `sound.raw` from the input H.264 path, so it no longer depends on a fixed working directory
- `gateway_client` depends on pkg-config modules for SDL2, FFmpeg (`libavcodec`, `libavutil`, `libswscale`), and `libusb-1.0`
- macOS host builds must use host compiler binaries from macOS but target runtime libraries from the SDK
- Docker is required on macOS for `elf2bin.linux` and `checksum.linux`
- CrossOver is the current path for running the Windows image downloader on macOS
