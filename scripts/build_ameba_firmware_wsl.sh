#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_ROOT="${AMEBA_SDK_ROOT:-$REPO_ROOT/.scratch/pristine_20260325_2/sdk-ameba-v5.2g_gcc}"
PROJECT_DIR="$SDK_ROOT/project/realtek_amebapro_v0_example/GCC-RELEASE"
TOOLCHAIN_ARCHIVE="$SDK_ROOT/tools/arm-none-eabi-gcc/asdk-6.4.1-linux-newlib-build-3026-x86_64.tar.bz2"
TOOLCHAIN_ROOT="/root/ameba_toolchains"
TOOLCHAIN_DIR="$TOOLCHAIN_ROOT/asdk-6.4.1/linux/newlib"
CMD_SHELL_SRC_OBJ="$SDK_ROOT/component/soc/realtek/8195b/app/shell/cmd_shell.o"
CMD_SHELL_DST_OBJ="$PROJECT_DIR/application_is/Debug/obj/cmd_shell.o"
JOBS="${JOBS:-1}"
CLEAN="${CLEAN:-0}"

resolve_desktop_flash()
{
	local win_profile=""
	local win_flash=""

	if [ -n "${DESKTOP_FLASH:-}" ]; then
		printf '%s\n' "$DESKTOP_FLASH"
		return
	fi

	if command -v cmd.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
		win_profile="$(cmd.exe /c "echo %USERPROFILE%" 2>/dev/null | tr -d '\r' | tail -n 1)"
		if [ -n "$win_profile" ]; then
			win_flash="${win_profile}\\Desktop\\flash_is.bin"
			wslpath "$win_flash"
			return
		fi
	fi

	printf '%s\n' "/mnt/c/Users/Public/Desktop/flash_is.bin"
}

DESKTOP_FLASH="$(resolve_desktop_flash)"

if [ ! -f "$TOOLCHAIN_ARCHIVE" ]; then
	echo "Missing SDK toolchain archive: $TOOLCHAIN_ARCHIVE" >&2
	exit 1
fi

mkdir -p "$TOOLCHAIN_ROOT"
if [ ! -x "$TOOLCHAIN_DIR/bin/arm-none-eabi-gcc" ]; then
	echo "[wsl-build] extracting Linux toolchain into $TOOLCHAIN_ROOT"
	tar -xjf "$TOOLCHAIN_ARCHIVE" -C "$TOOLCHAIN_ROOT"
fi

export PATH="$TOOLCHAIN_DIR/bin:$PATH"

echo "[wsl-build] using toolchain: $(command -v arm-none-eabi-gcc)"
echo "[wsl-build] building project: $PROJECT_DIR"

cd "$PROJECT_DIR"
if [ "$CLEAN" = "1" ]; then
	make -f application.lp.mk clean
	make -f application.is.mk clean
fi

make -f application.lp.mk all \
	-j"$JOBS" \
	ARM_GCC_TOOLCHAIN="$TOOLCHAIN_DIR/bin" \
	SDK_NEWLIB_ROOT="$TOOLCHAIN_DIR"

mkdir -p "$(dirname "$CMD_SHELL_DST_OBJ")"
if [ -f "$CMD_SHELL_SRC_OBJ" ] && [ ! -f "$CMD_SHELL_DST_OBJ" ]; then
	echo "[wsl-build] seeding cmd_shell.o into application_is/Debug/obj"
	cp "$CMD_SHELL_SRC_OBJ" "$CMD_SHELL_DST_OBJ"
fi

is_status=0
make -f application.is.mk all \
	-j"$JOBS" \
	ARM_GCC_TOOLCHAIN="$TOOLCHAIN_DIR/bin" \
	SDK_NEWLIB_ROOT="$TOOLCHAIN_DIR" || is_status=$?

if [ ! -f "$PROJECT_DIR/application_is/flash_is.bin" ]; then
	echo "[wsl-build] missing flash_is.bin after build" >&2
	exit "${is_status:-1}"
fi

mkdir -p "$(dirname "$DESKTOP_FLASH")"
cp "$PROJECT_DIR/application_is/flash_is.bin" "$DESKTOP_FLASH"
sha256sum "$PROJECT_DIR/application_is/flash_is.bin"
echo "[wsl-build] copied desktop image to $DESKTOP_FLASH"

if [ "$is_status" -ne 0 ]; then
	echo "[wsl-build] make returned $is_status, but flash_is.bin was produced; continuing" >&2
fi
