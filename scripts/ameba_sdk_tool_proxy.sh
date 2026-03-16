#!/bin/sh

set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <tool-name> [args...]" >&2
    exit 1
fi

tool_name="$1"
shift

if [ -z "${AMEBA_SDK_ROOT:-}" ]; then
    echo "AMEBA_SDK_ROOT is required" >&2
    exit 1
fi

sdk_root=$(cd "$AMEBA_SDK_ROOT" && pwd)
tool_dir="$sdk_root/component/soc/realtek/8195b/misc/iar_utility"
tool_path="$tool_dir/$tool_name"
container_tool="/opt/sdk/component/soc/realtek/8195b/misc/iar_utility/$tool_name"

if [ ! -f "$tool_path" ]; then
    echo "missing tool: $tool_path" >&2
    exit 1
fi

if [ ! -x "$tool_path" ]; then
    chmod +x "$tool_path"
fi

case "$(uname -s)" in
    Darwin)
        if ! command -v docker >/dev/null 2>&1; then
            echo "docker is required to run $tool_name on macOS" >&2
            exit 1
        fi

        case "$PWD" in
            "$sdk_root" | "$sdk_root"/*) ;;
            *)
                echo "$tool_name must be run from inside $sdk_root" >&2
                exit 1
                ;;
        esac

        rel_cwd="${PWD#$sdk_root}"
        if [ -n "$rel_cwd" ]; then
            container_wd="/opt/sdk$rel_cwd"
        else
            container_wd="/opt/sdk"
        fi

        exec docker run --rm \
            --platform linux/amd64 \
            -u "$(id -u):$(id -g)" \
            -v "$sdk_root:/opt/sdk" \
            -w "$container_wd" \
            ubuntu:22.04 \
            "$container_tool" "$@"
        ;;
    Linux)
        exec "$tool_path" "$@"
        ;;
    *)
        echo "unsupported host OS: $(uname -s)" >&2
        exit 1
        ;;
esac
