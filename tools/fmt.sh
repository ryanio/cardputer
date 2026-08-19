#!/usr/bin/env bash
# Apply .clang-format to the firmware and the simulator.
#
#   tools/fmt.sh           format in place
#   tools/fmt.sh --check   list what is unformatted and exit non zero, for CI
#
# Generated files are skipped. They come out of their own scripts and get
# rewritten wholesale, so formatting them would only make the next generation
# look like a change.
set -euo pipefail

cd "$(dirname "$0")/.."

GENERATED=(src/icons.h src/glyphs.h sim/src/fixtures.h)

find_clang_format() {
	if command -v clang-format >/dev/null 2>&1; then
		command -v clang-format
		return
	fi
	# macOS carries one inside Xcode without putting it on the path.
	if xcrun --find clang-format >/dev/null 2>&1; then
		xcrun --find clang-format
		return
	fi
	echo "no clang-format found. brew install clang-format, or pip install clang-format" >&2
	exit 2
}

CF="$(find_clang_format)"
CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

is_generated() {
	local file="$1"
	for skip in "${GENERATED[@]}"; do
		[ "$file" = "$skip" ] && return 0
	done
	return 1
}

files=()
while IFS= read -r file; do
	is_generated "$file" || files+=("$file")
done < <(git ls-files '*.cpp' '*.h')

if [ "$CHECK" = "1" ]; then
	bad=0
	for file in "${files[@]}"; do
		if ! "$CF" "$file" | diff -q "$file" - >/dev/null; then
			echo "unformatted: $file"
			bad=$((bad + 1))
		fi
	done
	if [ "$bad" != "0" ]; then
		echo
		echo "$bad file(s) need tools/fmt.sh. $("$CF" --version)"
		exit 1
	fi
	echo "${#files[@]} files match .clang-format"
	exit 0
fi

"$CF" -i "${files[@]}"
echo "formatted ${#files[@]} files with $("$CF" --version)"
