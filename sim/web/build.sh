#!/usr/bin/env bash
# Compile the simulator to WebAssembly: the same firmware sources the device
# builds from, the same M5GFX, drawn into a canvas instead of a panel.
#
#   source ~/emsdk/emsdk_env.sh && sim/web/build.sh
#
# Output lands in site/sim/ and is committed, so the site never depends on a
# toolchain being present in CI.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
M5GFX=${M5GFX:-$ROOT/.pio/libdeps/sim/M5GFX/src}
JSON=${JSON:-$ROOT/.pio/libdeps/sim/ArduinoJson/src}
OUT=$ROOT/site/sim
OBJ=$ROOT/.pio/build/web

if [ ! -d "$M5GFX" ]; then
	echo "M5GFX not found at $M5GFX. Run: pio run -e sim" >&2
	exit 1
fi

mkdir -p "$OUT" "$OBJ"

INCLUDES="-I$ROOT/src -I$ROOT/sim/include -I$M5GFX -I$JSON"
DEFINES="-DM5GFX_BOARD=board_M5CardputerADV -DM5GFX_SCALE=3 \
	-DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 \
	-DARDUINOJSON_ENABLE_PROGMEM=0"
COMMON="-O2 -sUSE_SDL=2 $INCLUDES $DEFINES"

# C and C++ are compiled separately. M5GFX carries a few C files, and building
# those as C++ is a source of quiet breakage.
compile() {
	local src=$1 lang=$2
	local obj="$OBJ/$(echo "$src" | shasum | cut -c1-16).o"
	# A failure inside a command substitution does not trip set -e, and the
	# link then complains about a missing object rather than the compile that
	# never happened. So it is checked here.
	if [ "$lang" = "c" ]; then
		emcc $COMMON -c "$src" -o "$obj" || { echo "failed: $src" >&2; exit 1; }
	else
		em++ -std=gnu++17 $COMMON -c "$src" -o "$obj" || { echo "failed: $src" >&2; exit 1; }
	fi
	echo "$obj"
}

# Every firmware source except the two the simulator replaces, which is what
# platformio.ini's build_src_filter says for the native build. Listing them by
# hand meant the next file added to the spine broke this link, quietly, in the
# one build nobody compiles before pushing.
SOURCES_CPP=()
while IFS= read -r f; do
	case "$(basename "$f")" in
		main.cpp | net.cpp | jpeg.cpp) continue ;;
	esac
	SOURCES_CPP+=("$f")
done < <(find "$ROOT/src" -name '*.cpp')
# Same again for the simulator's own ring, minus the native entry point. Found
# rather than listed, for the reason above.
while IFS= read -r f; do
	case "$(basename "$f")" in
		main_native.cpp) continue ;;
	esac
	SOURCES_CPP+=("$f")
done < <(find "$ROOT/sim/src" -name '*.cpp')
while IFS= read -r f; do SOURCES_CPP+=("$f"); done < <(find "$M5GFX" -name '*.cpp')

SOURCES_C=()
while IFS= read -r f; do SOURCES_C+=("$f"); done < <(find "$M5GFX" -name '*.c')

echo "compiling $((${#SOURCES_CPP[@]} + ${#SOURCES_C[@]})) files"
OBJECTS=()
for f in "${SOURCES_C[@]}"; do OBJECTS+=("$(compile "$f" c)"); done
for f in "${SOURCES_CPP[@]}"; do OBJECTS+=("$(compile "$f" cpp)"); done

echo "linking"
em++ "${OBJECTS[@]}" -o "$OUT/coral.js" \
	-O2 -sUSE_SDL=2 \
	-sALLOW_MEMORY_GROWTH=1 \
	-sMODULARIZE=1 -sEXPORT_NAME=CoralSim \
	-sEXPORTED_FUNCTIONS=_main,_simPress \
	-sEXPORTED_RUNTIME_METHODS=ccall \
	-sENVIRONMENT=web

ls -la "$OUT"
