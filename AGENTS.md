# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Build Commands

### Building the Project
```bash
cd build
cmake ..
make
```

### Clean Build
```bash
cd build
rm -rf *
cmake ..
make
```

### Building WebSocket Server (ws_server)
```bash
cd ws_server/build
cmake ..
make
```

## Project Architecture

### Core Components

**tinyhttpd** - A minimal HTTP server library designed for modularity and performance
- Core implementation: `minihttpd.cpp` / `minihttpd.h`
- Multi-threaded capability for better performance
- Custom STL-like implementations in `ministd/` (string, vector, map, etc.)
- Designed to work with nginx as a service module

**genbin** - Static resource generator
- Embeds web assets from `htdocs/` into binary format
- Generates `htdocs_bin.c` with xxd for embedded systems
- Handles MIME type detection and compression (gzip for text files)

**ws_server** - WebSocket server with video streaming capabilities
- Separate CMake project with OpenCV dependency
- Includes H.264 video streaming (`wsh264/`)
- JSON processing with cJSON
- Base64, SHA1, UTF8 utilities for WebSocket protocol
- Vendored directly in this repository; no submodule setup is required

### Web Interface

The `htdocs/` directory is now just a leftover minimal browser viewer asset set:
- `main.html` is the single entry page
- JavaScript is trimmed to websocket connect, ChaCha20 decrypt, H.264 decode/display, and PCM audio playback
- The old `ameba/` HTTP server integration path has been removed

### Cross-Platform Build System

- **Native builds**: Uses system GCC/G++ (macOS, Linux)
- **ARM embedded**: Configurable ARM toolchain support for embedded targets
- **Build modes**: Debug (-Og -g) and Release (-Os -flto) configurations
- **Custom commands**: Resource generation via `genbin`; active runtime build flow lives under `ws_server/`

### Custom Standard Library (ministd)

Custom implementations to avoid standard library dependencies:
- `mystring.hpp` - String class with manual memory management
- `myvector.hpp` - Dynamic array container
- `mymap.hpp` - Key-value mapping using AVL tree (`avl_bf.c`)
- `mylist.hpp` - Linked list implementation
- Designed for embedded systems with minimal overhead

## Key Development Notes

- The project supports both native development and ARM embedded targets
- The actively maintained runtime path is `wsh264` + `gateway` + `gateway_client` plus the AmebaPro SDK firmware build under `./.local/sdk-ameba-v5.2g_gcc/`
- Security warnings about `sprintf` usage exist - consider replacing with `snprintf`
- WebSocket server requires OpenCV for video processing features
