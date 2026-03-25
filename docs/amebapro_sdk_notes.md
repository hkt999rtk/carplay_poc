# AmebaPro SDK Notes

This note records where the AmebaPro SDK lives in this workspace and how we are using it for firmware builds.

## SDK Location

- SDK archive in repo:
  - `tools/sdk-ameba-v5.2g_gcc.tar.gz`
- Extracted SDK root used in this workspace:
  - `.local/sdk-ameba-v5.2g_gcc`
- Firmware project we are building:
  - `.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE`

## Gateway Firmware Hook

The current minimal gateway firmware entry lives in the main repo, not inside the SDK tree:

- `ameba_gateway/example_gateway_ameba.c`
- `ameba_gateway/example_gateway_ameba.h`

It is hooked into the SDK example framework through:

- `.local/sdk-ameba-v5.2g_gcc/component/common/example/example_entry.c`
- `.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/inc/platform_opts.h`
- `.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application.is.mk`

## What The Current Firmware Does

This is still a bring-up image for `gateway`, not the full relay yet.

Current debug behavior:

- early boot prints from `main.c`
- `gateway` FreeRTOS task heartbeat every second
- `wlan_network()` is enabled again
- `CONFIG_FAST_DHCP` is disabled to avoid a boot-time Usage Fault in `wifi_is_connected_to_ap()`

Typical UART lines:

- `[gateway][boot] stage=early counter=%d`
- `[gateway][boot] stage=before_wlan_network`
- `[gateway][boot] stage=after_wlan_network`
- `[gateway] AmebaPro gateway firmware booted`
- `[gateway] heartbeat counter=%lu tick=%lu heap=%lu`

## Build Outputs

Main output files:

- `.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/flash_is.bin`
- `.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/firmware_is.bin`
- `.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/ota_is.bin`
- `.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/Debug/bin/application_is.axf`

If you are flashing the full image, use `flash_is.bin` first.

Convenience copy used for flashing from Windows desktop:

- `C:\Users\hkt99\Desktop\flash_is.bin`
- known-good USB baseline snapshot:
  - `C:\Users\hkt99\Desktop\flash_is_usb_pingpong_baseline.bin`

After each successful rebuild, copy the new `flash_is.bin` from the SDK output directory to the desktop copy.

## Known-Good USB Baseline

Use this section as the rollback target if later USB experiments break enumeration or bulk traffic.

- Baseline image path:
  - `C:\Users\hkt99\Desktop\flash_is_usb_pingpong_baseline.bin`
- Baseline SHA256:
  - `47A264B0E5185781C9AD64AAD996359B6A1DD719367ACA72A1E50AACDA029572`
- Expected Windows enumeration:
  - `AmebaPro Gateway`
  - `USB\VID_0BDA&PID_8195\0123456789`
  - `WinUSB`
- Expected host test result:
  - `gateway_client.exe --transport usb --usb-ping --usb-ping-count 1`
  - prints `usb ping/pong ok seq=0 payload="gateway-usb-pong"`
- Expected UART state after a successful single ping:
  - `usb_started=1 usb_cfg=1 rx=1 tx=1`
- Important limitation of this baseline:
  - USB transport is working end-to-end
  - firmware still does not decode the incoming `PING` payload correctly
  - current bring-up logic sends `PONG` even when `magic` is not parsed as `PING`
- Most recent diagnostic finding on top of this baseline:
  - `req->buf` and `ctx->out_buf` are the same address
  - both show `00 00 00 00 ...` after a successful `actual=64` OUT transfer
  - adding `dcache_invalidate_by_addr()` before reading the OUT buffer did not change that result
  - next likely direction is to inspect how SDK `usbd_msc` / `usbd_blank` actually retrieves bulk OUT payload data

## USB Root Cause And Fix

This section records the actual root cause of the USB payload decode problem and the final working fix.

### Final Symptom Pattern

The system could reach a partially-working state where all of the following were true at the same time:

- Windows enumerated the device correctly as `AmebaPro Gateway`
- the `WinUSB` driver attached correctly
- `gateway_client.exe --transport usb --usb-ping --usb-ping-count 1` reported success and received `PONG`
- firmware UART showed:
  - `usb_started=1 usb_cfg=1`
  - `rx=1 tx=1`
  - `actual=64`

But the firmware still printed:

- `magic=00 00 00 00`
- `bytes=00 00 00 00`
- `last_valid=2`

That meant:

- USB bulk OUT transactions were reaching the device
- the firmware callback was running
- but the payload bytes visible to CPU code were still not the expected `"PING"`

### What Was Ruled Out

The following possibilities were tested and ruled out:

- Host side packet generation was wrong:
  - `gateway_client` really does send `"PING"` in the first 4 bytes of the 64-byte packet.
- Wrong buffer pointer:
  - instrumentation showed `req->buf` and `ctx->out_buf` were the same address.
- Simple stale D-cache only:
  - adding `dcache_invalidate_by_addr()` before reading the OUT buffer did not change the observed `00 00 00 00`.
- Endpoint allocator / alternative DMA buffer experiments:
  - trying `alloc_buffer` / DMA-safe allocator paths either failed allocation or destabilized USB enumeration.

### Key Discovery From SDK `usbd_msc`

The useful reference was not source code in the repo tree, because the class implementation is prebuilt inside:

- `.local/sdk-ameba-v5.2g_gcc/component/soc/realtek/8195b/misc/bsp/lib/common/GCC/lib_usbd.a`

The relevant objects were extracted and inspected:

- `usbd_blank.o`
- `usbd_msc.o`

The critical discovery from the `usbd_msc` implementation was:

- it does not only assign `req->buf`
- it also assigns `req->dma` to the same buffer address

In other words, the working SDK path effectively does this:

```c
req->buf = some_buffer;
req->dma = (dma_addr_t)some_buffer;
req->complete = ...;
req->context = ...;
```

That was the important difference from the custom gateway USB function.

### Actual Root Cause

The custom bulk function was queueing USB requests with:

- `req->buf` set
- `req->complete` set
- `req->context` set

but **without setting `req->dma`**.

On this AmebaPro USB stack, that was enough for transactions to appear to complete, but not enough for the OUT payload to become visible in the expected request buffer.

As a result:

- the transfer completed
- `req->actual` was correct
- `rx` increased
- but the first bytes still looked like zeroes

### Final Fix

The working fix was to set `req->dma` for both IN and OUT requests to the same backing buffer used by `req->buf`.

In `ameba_gateway/example_gateway_ameba.c`, the fix is:

```c
ctx->in_req->buf = ctx->in_buf;
ctx->in_req->dma = (dma_addr_t)ctx->in_buf;

ctx->out_req->buf = ctx->out_buf;
ctx->out_req->dma = (dma_addr_t)ctx->out_buf;
```

This is now done both:

- when configuring the requests in `gateway_usb_function_set_alt()`
- when queueing the next OUT request in `gateway_usb_queue_out_request()`
- when preparing the IN request in `gateway_usb_send_pong()`

### Verification After Fix

After setting `req->dma`, the firmware finally reported the expected payload:

- `last_valid=1`
- `magic=50 49 4e 47`
- `bytes=50 49 4e 47`
- `payload=16`

and the buffer trace showed:

- `req->buf == ctx->out_buf`
- both contained `"PING"` followed by zeros for the sequence field

At the same time, the host still printed:

- `usb ping/pong ok seq=0 payload="gateway-usb-pong"`

So after the fix, the path is now genuinely:

- host sends `PING`
- firmware reads real `PING`
- firmware replies `PONG`

### Current Working Image

The current image that includes the working `req->dma` fix is:

- `C:\Users\hkt99\Desktop\flash_is.bin`

SHA256:

- `55087C830F676D12EDA0081D21F5930DCD26C4899CF13E7834F1469681E20EB7`

Expected successful UART proof after one host USB ping:

- `usb_started=1 usb_cfg=1 rx=1 tx=1`
- `last_valid=1`
- `magic=50 49 4e 47`

### Practical Lesson

For this SDK and controller, when implementing a custom USB bulk function, do not assume that setting only `req->buf` is enough.

The minimum safe rule is:

- if you provide a manually-managed request buffer
- set both `req->buf` and `req->dma`
- keep them pointing to the same backing storage

If a future USB experiment regresses back to:

- enumeration works
- `actual` is non-zero
- but payload bytes appear as `00 00 00 00`

then the first thing to verify is whether `req->dma` is still being assigned consistently.

## Linux Build

From the firmware project directory:

```bash
cd ./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE
make clean
make
```

## Windows Host Notes

This workspace does not include the original SDK Windows toolchain zip.
The current Windows-host build that produced the firmware image used:

- frontend compiler from MSYS2:
  - `C:/msys64/ucrt64/bin/arm-none-eabi-gcc.exe`
- target runtime libraries extracted from the SDK Linux archive into:
  - `.local/sdk-ameba-v5.2g_gcc/tools/arm-none-eabi-gcc/asdk/linux/newlib`

The SDK makefiles were adjusted so they can:

- accept an overridden `ARM_GCC_TOOLCHAIN`
- find generated `.i/.s` files safely with newer GCC
- link against the SDK-provided `newlib`/`libgcc` paths

Windows host build command that produced the working image:

```powershell
C:\msys64\usr\bin\bash.exe -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; cd /c/Users/hkt99/work/carplay_poc/.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE && /ucrt64/bin/mingw32-make.exe -f application.is.mk all ARM_GCC_TOOLCHAIN=/ucrt64/bin ELF2BIN=../../../component/soc/realtek/8195b/misc/iar_utility/elf2bin.exe CHKSUM=../../../component/soc/realtek/8195b/misc/iar_utility/checksum.exe"
```

Important Windows-specific caveats:

- When invoking `cmake`, `ninja`, `gcc`, or `gateway_client.exe` from a plain PowerShell window, prepend:
  - `C:\msys64\ucrt64\bin`
  - `C:\msys64\usr\bin`
  to `PATH`
- If `gateway_client` or `c++` fails with almost no diagnostics, check `cc1plus.exe` dependencies first. In this workspace the failure was caused by `cc1plus.exe` not finding UCRT64 runtime DLLs because `PATH` did not include `C:\msys64\ucrt64\bin`.

- `application.is.mk` originally referenced a missing DSP assembly file `arm_bitreversal2.S`; remove that `SRC_ASM` entry when rebuilding in this workspace.
- `postbuild.sh` may segfault during `ota_is.bin` merge on Windows. This does not block use of `flash_is.bin` if it has already been generated successfully.
- The desktop flashing workflow should use `C:\Users\hkt99\Desktop\flash_is.bin`, not the shortcut.

Example PowerShell session for host-side tools:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:PATH
& 'C:\msys64\ucrt64\bin\cmake.exe' --build build/windows-ucrt64 --target gateway_client
```

## Windows Customer Package

When packaging Windows binaries for a customer, do not ship only the `.exe` files by themselves.
Use the package script so the required UCRT64 runtime DLLs are collected automatically:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows_demo.ps1
```

Current package output:

- `dist/windows-demo-package`
- `dist/windows-demo-package.zip`

At minimum, the package must include:

- `wsh264.exe`
- `gateway.exe`
- `gateway_client.exe`
- `config.json`
- `test_data\`
- MSYS2 UCRT64 runtime DLLs copied next to the executables

Important runtime DLLs observed from the current build:

- `SDL2.dll`
- `avcodec-62.dll`
- `avutil-60.dll`
- `swscale-9.dll`
- `libusb-1.0.dll`
- `libgcc_s_seh-1.dll`
- `libstdc++-6.dll`
- `libwinpthread-1.dll`

There are also many transitive FFmpeg/SDL/font/image codec DLLs. The package script already resolves and copies them, so the safest rule is:

- deliver the whole packaged `bin\` folder from `package_windows_demo.ps1`
- do not try to hand-pick only a few DLLs
- Windows system DLLs like `ucrtbase.dll`, `KERNEL32.DLL`, `WS2_32.dll` are not bundled

## UART / Serial Notes

- Known-good reader settings for verification:
  - `COM3`
  - `115200`
  - `8N1`
  - no flow control
- PowerShell can read the board UART directly when `PuTTY` is fully closed; only one process can own `COM3` at a time.
- A successful runtime verification captured lines like:
  - `[gateway] heartbeat counter=39 tick=40129 heap=32593664`
  - `[gateway] heartbeat counter=40 tick=41132 heap=32593664`

## Fault Note

One boot failure was traced to:

- `init_thread` in `.local/sdk-ameba-v5.2g_gcc/component/common/api/network/src/wlan_network.c:69`
- faulting inside `wifi_is_connected_to_ap()`
- symptom: `Usage Fault` / `Unaligned access UsageFault`

Current workaround:

- keep `wlan_network()` enabled
- disable fast-connect path by setting `CONFIG_EXAMPLE_WLAN_FAST_CONNECT 0` so `CONFIG_FAST_DHCP` becomes `0`

## Quick Verification

To check that the built firmware still contains the expected UART strings:

```powershell
C:\msys64\ucrt64\bin\strings.exe `
  .\.local\sdk-ameba-v5.2g_gcc\project\realtek_amebapro_v0_example\GCC-RELEASE\application_is\Debug\bin\application_is.axf `
  | Select-String '\[gateway\]|AmebaPro gateway firmware booted|UART log is alive|heartbeat counter|before_wlan_network'
```

## USB Ping/Pong Bring-Up

Minimal host-side USB test command:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:PATH
.\build\windows-ucrt64\bin\gateway_client.exe --transport usb --usb-ping --usb-ping-count 10
```

Current USB bring-up expectations:

- firmware uses `usbd_blank` bulk endpoints with VID/PID `0x0BDA:0x8195`
- firmware overrides the `usbd_blank` interface descriptor to `vendor-specific` before `usbd_blank_init()`, so Windows can bind it to `WinUSB/libusb` more naturally
- `gateway_client` now accepts a bulk IN/OUT interface for that VID/PID and can do fixed-size `PING`/`PONG` bulk exchanges
- the firmware responds to a fixed-size bulk `PING` packet with a `PONG` packet and logs activity on UART
- the current working custom bulk function requires both `req->buf` and `req->dma` to be assigned to the same buffer for USB payload bytes to decode correctly

## Reminder

The SDK tree under `.local/` is local workspace state and is not meant to be treated as clean source-of-truth.
If the SDK is re-extracted, re-check:

- `example_entry.c`
- `platform_opts.h`
- `application.is.mk`
- `application.lp.mk`
