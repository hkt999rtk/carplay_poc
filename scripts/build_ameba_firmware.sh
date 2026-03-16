#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
sdk_root="${AMEBA_SDK_ROOT:-$repo_root/.local/sdk-ameba-v5.2g_gcc}"
build_dir="$sdk_root/project/realtek_amebapro_v0_example/GCC-RELEASE"
macos_flash_template="${AMEBA_FLASH_TEMPLATE:-$build_dir/FW/1115_EVB_WITHOUT_DEVID/flash_is.bin}"
sdk_archive="$sdk_root/tools/arm-none-eabi-gcc/asdk-6.4.1-linux-newlib-build-3026-x86_64.tar.bz2"
sdk_linux_parent="$sdk_root/.codex-macos-sdk-toolchain"
sdk_linux_toolchain="$sdk_linux_parent/asdk-6.4.1/linux/newlib"
macos_host_toolchain=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --macos-host-toolchain)
            macos_host_toolchain=1
            shift
            ;;
        --)
            shift
            break
            ;;
        *)
            break
            ;;
    esac
done

if [ ! -d "$build_dir" ]; then
    echo "missing SDK build directory: $build_dir" >&2
    echo "extract tools/sdk-ameba-v5.2g_gcc.tar.gz into ./.local first" >&2
    exit 1
fi

ensure_sdk_linux_toolchain() {
    if [ -d "$sdk_linux_toolchain" ]; then
        return
    fi

    if [ ! -f "$sdk_archive" ]; then
        echo "missing SDK toolchain archive: $sdk_archive" >&2
        exit 1
    fi

    mkdir -p "$sdk_linux_parent"
    tar -xjf "$sdk_archive" -C "$sdk_linux_parent"
}

build_macos_compat_obj() {
    output="$1"
    shift
    "$toolchain_dir/arm-none-eabi-gcc" "$@" -c "$script_dir/ameba_newlib_compat.c" -o "$output"
}

run_make() {
    make -C "$build_dir" "$@"
}

run_macos_linux_binary() {
    rel_tool="$1"
    shift
    tool_path="$sdk_root/$rel_tool"

    if [ ! -f "$tool_path" ]; then
        echo "missing SDK Linux tool: $tool_path" >&2
        exit 1
    fi

    if [ ! -x "$tool_path" ]; then
        chmod +x "$tool_path"
    fi

    docker run --rm \
        --platform linux/amd64 \
        -u "$(id -u):$(id -g)" \
        -v "$sdk_root:/opt/sdk" \
        -w /opt/sdk \
        ubuntu:22.04 \
        "/opt/sdk/$rel_tool" "$@"
}

run_macos_lp_make() {
    run_make -f application.lp.mk \
        "ARM_GCC_TOOLCHAIN=$toolchain_dir" \
        "LFLAGS=$lp_lflags" \
        "LIBFLAGS=$lp_libflags" \
        "ROMIMG=$lp_compat" \
        "$@"
}

run_macos_is_make() {
    run_make -f application.is.mk \
        "ARM_GCC_TOOLCHAIN=$toolchain_dir" \
        "ELF2BIN=$script_dir/elf2bin.macos" \
        "CHKSUM=$script_dir/checksum.macos" \
        "LFLAGS=$is_lflags" \
        "LIBFLAGS=$is_libflags" \
        "ROMIMG=$is_compat" \
        "$@"
}

select_macos_sensor_image() {
    sensor_header="$sdk_root/project/realtek_amebapro_v0_example/inc/sensor.h"
    ispfw_path="$sdk_root/component/soc/realtek/8195b/misc/bsp/image"
    sensor_token=$(tr -d '\r' < "$sensor_header" | awk '$1 == "#define" && $2 == "SENSOR_USE" { print $3; exit }')

    if [ -z "$sensor_token" ]; then
        echo "failed to determine SENSOR_USE from $sensor_header" >&2
        exit 1
    fi

    sensor_name=$(printf '%s' "$sensor_token" | sed 's/^SENSOR_//' | tr '[:upper:]' '[:lower:]')

    if [ "$sensor_name" = "all" ]; then
        sensor_config="$ispfw_path/sensor_config.h"
        tr -d '\r' < "$sensor_header" | awk '
            $1 == "#define" && $2 ~ /^SENSOR_[A-Z0-9]+$/ && $3 ~ /^0x/ {
                sensor = tolower(substr($2, 8))
                if (sensor != "all") {
                    print $3 "," sensor
                }
            }
        ' > "$sensor_config"

        run_macos_linux_binary \
            "component/soc/realtek/8195b/misc/bsp/image/ISP_COMBINE_LINUX" \
            "/opt/sdk/component/soc/realtek/8195b/misc/bsp/image/sensor_config.h" \
            "/opt/sdk/component/soc/realtek/8195b/misc/bsp/image/"
        return
    fi

    sensor_bin="$ispfw_path/isp_$sensor_name.bin"
    if [ ! -f "$sensor_bin" ]; then
        echo "missing sensor image: $sensor_bin" >&2
        exit 1
    fi

    cp "$sensor_bin" "$ispfw_path/isp.bin"
}

partition_json_hex_field() {
    object_name="$1"
    field_name="$2"

    tr -d '\r' < "$build_dir/partition.json" | awk -v object="$object_name" -v field="$field_name" '
        $1 == "\"" object "\":" {
            in_object = 1
            next
        }
        in_object && $1 == "}," {
            in_object = 0
        }
        in_object && $1 == "\"" field "\":" {
            gsub(/"|,/, "", $2)
            print $2
            exit
        }
    '
}

build_macos_flash_image() {
    boot_bin="$sdk_root/component/soc/realtek/8195b/misc/bsp/image/boot.bin"
    flash_out="$build_dir/application_is/flash_is.bin"
    fw_bin="$build_dir/application_is/firmware_is.bin"
    boot_offset_hex=$(partition_json_hex_field boot start_addr)
    fw1_offset_hex=$(partition_json_hex_field fw1 start_addr)

    if [ -z "$boot_offset_hex" ] || [ -z "$fw1_offset_hex" ]; then
        echo "failed to parse boot/fw1 offsets from $build_dir/partition.json" >&2
        exit 1
    fi

    if [ ! -f "$macos_flash_template" ]; then
        echo "missing flash template: $macos_flash_template" >&2
        exit 1
    fi

    boot_offset=$((boot_offset_hex))
    fw1_offset=$((fw1_offset_hex))

    dd if="$macos_flash_template" of="$flash_out" bs=1 count="$fw1_offset" 2>/dev/null
    dd if="$boot_bin" of="$flash_out" bs=1 seek="$boot_offset" conv=notrunc 2>/dev/null
    cat "$fw_bin" >> "$flash_out"
}

package_macos_firmware_images() {
    (
        cd "$build_dir"
        "$script_dir/elf2bin.macos" keygen keycfg.json
        "$script_dir/elf2bin.macos" convert amebapro_firmware_is.json FIRMWARE
        cp ../../../component/soc/realtek/8195b/misc/bsp/image/boot.bin application_is/boot.bin
        "$script_dir/checksum.macos" application_is/firmware_is.bin
        build_macos_flash_image
        sh ../../../component/soc/realtek/8195b/misc/gcc_utility/postbuild.sh "$script_dir/elf2bin.macos"
    )
}

build_macos_firmware() {
    ensure_sdk_linux_toolchain

    lp_runtime_dir="$sdk_linux_toolchain/arm-none-eabi/lib/v8-m.base"
    lp_gcc_dir="$sdk_linux_toolchain/lib/gcc/arm-none-eabi/6.4.1/v8-m.base"
    is_runtime_dir="$sdk_linux_toolchain/arm-none-eabi/lib/v8-m.main/softfp/fpv5-sp-d16"
    is_gcc_dir="$sdk_linux_toolchain/lib/gcc/arm-none-eabi/6.4.1/v8-m.main/softfp/fpv5-sp-d16"

    lp_compat="$build_dir/macos_toolchain_compat_lp.o"
    is_compat="$build_dir/macos_toolchain_compat_is.o"

    build_macos_compat_obj "$lp_compat" -march=armv8-m.base -mthumb
    build_macos_compat_obj "$is_compat" -march=armv8-m.main+dsp -mthumb -mcmse -mfloat-abi=softfp -mfpu=fpv5-sp-d16

    export AMEBA_SDK_ROOT="$sdk_root"

    lp_lflags="-L$lp_gcc_dir -L$lp_runtime_dir -O2 -march=armv8-m.base -mthumb -mcmse -nostartfiles -nodefaultlibs -nostdlib --specs=$lp_runtime_dir/nosys.specs -Wl,--gc-sections -Wl,--warn-section-align -Wl,--cref -Wl,--build-id=none -Wl,--use-blx -Wl,-Map=application_lp/Debug/bin/application_lp.map"
    lp_libflags="../../../component/soc/realtek/8195b/fwlib/hal-rtl8195b-lp/lib/lib/hal_pmc_lp.a ../../../component/soc/realtek/8195b/misc/bsp/lib/common/GCC/lib_soc_lp.a $lp_runtime_dir/libm.a $lp_runtime_dir/libc.a $lp_runtime_dir/libnosys.a $lp_gcc_dir/libgcc.a $lp_runtime_dir/libstdc++.a"

    is_lflags="-L$is_gcc_dir -L$is_runtime_dir -march=armv8-m.main+dsp -mthumb -mcmse -mfloat-abi=softfp -mfpu=fpv5-sp-d16 -Os -nostartfiles --specs=$is_runtime_dir/nosys.specs -nodefaultlibs -nostdlib -Wl,--gc-sections -Wl,-Map=application_is/Debug/bin/application_is.map -Wl,--cref -Wl,--build-id=none -Wl,--use-blx -Wl,-wrap,strcat -Wl,-wrap,strchr -Wl,-wrap,strcmp -Wl,-wrap,strncmp -Wl,-wrap,strnicmp -Wl,-wrap,strcpy -Wl,-wrap,strncpy -Wl,-wrap,strlcpy -Wl,-wrap,strlen -Wl,-wrap,strnlen -Wl,-wrap,strncat -Wl,-wrap,strpbrk -Wl,-wrap,strspn -Wl,-wrap,strstr -Wl,-wrap,strtok -Wl,-wrap,strxfrm -Wl,-wrap,strsep -Wl,-wrap,strtod -Wl,-wrap,strtof -Wl,-wrap,strtold -Wl,-wrap,strtoll -Wl,-wrap,strtoul -Wl,-wrap,strtoull -Wl,-wrap,atoi -Wl,-wrap,atoui -Wl,-wrap,atol -Wl,-wrap,atoul -Wl,-wrap,atoull -Wl,-wrap,atof -Wl,-wrap,malloc -Wl,-wrap,realloc -Wl,-wrap,free -Wl,-wrap,memcmp -Wl,-wrap,memcpy -Wl,-wrap,memmove -Wl,-wrap,memset -Wl,-wrap,printf -Wl,-wrap,sprintf -Wl,-wrap,snprintf -Wl,-wrap,vsnprintf -Wl,-wrap,vprintf -Wl,-wrap,abort -Wl,-wrap,puts -Wl,-wrap,fopen -Wl,-wrap,fclose -Wl,-wrap,fread -Wl,-wrap,fwrite -Wl,-wrap,fseek -Wl,-wrap,fflush -Wl,-wrap,rename -Wl,-wrap,feof -Wl,-wrap,ferror -Wl,-wrap,ftell -Wl,-wrap,fputc -Wl,-wrap,fputs -Wl,-wrap,fgets -Wl,-wrap,remove -Wl,-wrap,opendir -Wl,-wrap,readdir -Wl,-wrap,closedir -Wl,-wrap,scandir -Wl,-wrap,rmdir -Wl,-wrap,mkdir -Wl,-wrap,access -Wl,-wrap,stat"
    is_libflags="../../../component/soc/realtek/8195b/fwlib/hal-rtl8195b-hp/lib/lib/hal_pmc_hs.a -L../../../component/soc/realtek/8195b/misc/bsp/lib/common/GCC/ -Wl,-u,ram_start -Wl,-u,cinit_start $is_runtime_dir/libm.a $is_runtime_dir/libc.a $is_runtime_dir/libnosys.a $is_gcc_dir/libgcc.a $is_runtime_dir/libstdc++.a -l_codec -l_dct -l_faac -l_h264 -l_haac -l_http -l_mmf -l_muxer -l_p2p -l_rtsp -l_sdcard -l_soc_is -l_speex -l_usbd -l_websocket -l_wlan -l_wps -l_qr_code -l_mdns -l_tftp -lrtstream -lrtscamkit -lrtsv4l2 -lrtsisp -lrtsosd"

    if [ "$#" -eq 0 ]; then
        set -- all
    fi

    case "$1" in
        all)
            run_macos_lp_make clean all
            run_macos_is_make clean prebuild build_info application
            select_macos_sensor_image
            package_macos_firmware_images
            ;;
        clean)
            run_make -f application.lp.mk clean
            run_make -f application.is.mk clean
            ;;
        clean_lp)
            run_make -f application.lp.mk clean
            ;;
        clean_is)
            run_make -f application.is.mk clean
            ;;
        lp|ram_lp)
            run_macos_lp_make clean all
            ;;
        is|ram_is)
            if [ ! -f "$build_dir/application_lp/Debug/bin/application_lp.axf" ]; then
                run_macos_lp_make clean all
            fi
            run_macos_is_make clean prebuild build_info application
            select_macos_sensor_image
            package_macos_firmware_images
            ;;
        package_is)
            if [ ! -f "$build_dir/application_is/Debug/bin/application_is.axf" ]; then
                echo "missing application_is.axf; run 'is' or 'all' first" >&2
                exit 1
            fi
            select_macos_sensor_image
            package_macos_firmware_images
            ;;
        *)
            echo "unsupported macOS target: $1" >&2
            echo "supported targets: all, clean, clean_lp, clean_is, lp, ram_lp, is, ram_is, package_is" >&2
            exit 1
            ;;
    esac
}

case "$(uname -s)" in
    Darwin)
        if [ "$macos_host_toolchain" -ne 1 ]; then
            echo "macOS firmware build requires --macos-host-toolchain" >&2
            exit 1
        fi
        toolchain_dir="${ARM_GCC_TOOLCHAIN:-/Applications/ARM/bin}"
        if [ ! -x "$toolchain_dir/arm-none-eabi-gcc" ]; then
            echo "missing arm-none-eabi-gcc under $toolchain_dir" >&2
            exit 1
        fi
        if ! command -v gawk >/dev/null 2>&1; then
            echo "gawk is required on macOS" >&2
            exit 1
        fi
        if ! command -v docker >/dev/null 2>&1; then
            echo "docker is required on macOS" >&2
            exit 1
        fi

        if [ "$#" -eq 2 ] && [ "$1" = "clean" ] && [ "$2" = "all" ]; then
            set --
        fi

        build_macos_firmware "$@"
        ;;
    Linux)
        if [ "$#" -eq 0 ]; then
            set -- clean all
        fi
        exec make -C "$build_dir" "$@"
        ;;
    *)
        echo "unsupported host OS: $(uname -s)" >&2
        exit 1
        ;;
esac
