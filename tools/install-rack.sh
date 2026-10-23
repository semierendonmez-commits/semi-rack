#!/usr/bin/env bash
#
# Build SEMI Family for VCV Rack 2 and install it into your Rack user folder,
# so the modules show up in Rack's module browser (brand "SEMI").
#
#   tools/install-rack.sh ~/Downloads/Rack-SDK
#
# The Rack SDK must match your computer: Rack-SDK-2.6.x-mac-arm64 on an Apple
# Silicon Mac, -mac-x64 on an Intel Mac, -lin-x64, -win-x64 (in MSYS2 MINGW64).
# Download it from https://vcvrack.com/downloads/
#
# macOS needs:  xcode-select --install   and   brew install zstd
# Linux needs:  build-essential zstd

set -euo pipefail

SDK="${1:-${RACK_DIR:-}}"
if [ -z "$SDK" ] || [ ! -f "$SDK/plugin.mk" ]; then
	echo "usage: $0 /path/to/Rack-SDK   (the folder that contains plugin.mk)" >&2
	exit 2
fi
SDK="$(cd "$SDK" && pwd)"
cd "$(dirname "$0")/.."

missing=()
for tool in make zstd; do
	command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if ! command -v c++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
	missing+=("a C++ compiler")
fi
if [ ${#missing[@]} -gt 0 ]; then
	echo "missing: ${missing[*]}" >&2
	case "$(uname -s)" in
		Darwin) echo "  xcode-select --install && brew install zstd" >&2 ;;
		Linux)  echo "  sudo apt install build-essential zstd" >&2 ;;
	esac
	exit 1
fi

# The SDK names its platform in the Rack library it ships; warn on a mismatch,
# which builds but cannot load (e.g. an x64 SDK on an Apple Silicon Mac).
case "$(uname -s)-$(uname -m)" in
	Darwin-arm64)  want=mac-arm64 ;;
	Darwin-x86_64) want=mac-x64 ;;
	Linux-x86_64)  want=lin-x64 ;;
	*)             want="" ;;
esac
if [ -n "$want" ] && [ -f "$SDK/libRack.dylib" ] && command -v lipo >/dev/null 2>&1; then
	arch="$(lipo -archs "$SDK/libRack.dylib" 2>/dev/null || true)"
	case "$want:$arch" in
		mac-arm64:*arm64*|mac-x64:*x86_64*) ;;
		*) echo "warning: this Rack SDK is for '$arch', this Mac needs $want" >&2 ;;
	esac
fi

jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
make -j"$jobs" install RACK_DIR="$SDK"

case "$(uname -s)" in
	Darwin) user="$HOME/Library/Application Support/Rack2" ;;
	Linux)  user="${XDG_DATA_HOME:-$HOME/.local/share}/Rack2" ;;
	*)      user="$LOCALAPPDATA/Rack2" ;;
esac
echo
echo "Installed: $(ls dist/*.vcvplugin)"
echo "      into: $user/plugins-$want/"
echo
echo "Quit and reopen VCV Rack. Rack unpacks the plugin on start; then right-click"
echo "the rack (or press Enter) and type SEMI in the module browser."
echo "If the modules do not appear, read $user/log.txt"
