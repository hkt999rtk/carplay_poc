# AGENTS.md

This repository keeps only:

- `wsh264/` and the minimal websocket/crypto support it needs
- `3rd_party/chacha` shared third-party dependency
- AmebaPro firmware build scripts and burn tooling

Legacy `tinyhttpd`, `genbin`, `htdocs`, `gateway`, `gateway_client`, `ministd`, and `ws_server` sources have been removed.

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

- The actively maintained host binary is `wsh264`
- `wsh264` derives `sound.raw` from the input H.264 path, so it no longer depends on a fixed working directory
- macOS host builds must use host compiler binaries from macOS but target runtime libraries from the SDK
- Docker is required on macOS for `elf2bin.linux` and `checksum.linux`
- CrossOver is the current path for running the Windows image downloader on macOS
