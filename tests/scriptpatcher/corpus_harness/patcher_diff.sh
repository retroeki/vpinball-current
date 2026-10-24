#!/usr/bin/env bash
# license:GPLv3+
#
# Which tables does the working-tree patcher change, compared with a git revision?
# Builds patch_corpus twice (REV's src/ui, and the working tree), patches every raw
# corpus with both, and lists the tables whose patched script differs
# (../patched_diff.py). Run in WSL.
#
#   ./patcher_diff.sh REV WORK RAW_DIR [RAW_DIR ...] [-- --expect TEXT ...]
#
# Exit status 1 when a table not named by --expect changed.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
REV="${1:?usage: patcher_diff.sh REV WORK RAW_DIR [RAW_DIR ...] [-- patched_diff args]}"
WORK="${2:?usage: patcher_diff.sh REV WORK RAW_DIR [RAW_DIR ...] [-- patched_diff args]}"
shift 2
RAWS=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do RAWS+=("$1"); shift; done
[ $# -gt 0 ] && shift
[ ${#RAWS[@]} -gt 0 ] || { echo "no RAW_DIR given" >&2; exit 2; }

mkdir -p "$WORK/base_src"
rm -rf "$WORK/base_src/src"
git -C "$ROOT" archive "$REV" src/ui | tar -x -C "$WORK/base_src"

echo "=== build the patcher of $REV and of the working tree ==="
cmake -S "$HERE" -B "$WORK/build_base" -DCMAKE_BUILD_TYPE=Release -DPATCHER_SRC="$WORK/base_src/src" > /dev/null
cmake --build "$WORK/build_base" --target patch_corpus -j"$(nproc)" > /dev/null 2>&1
cmake -S "$HERE" -B "$WORK/build_new" -DCMAKE_BUILD_TYPE=Release -DPATCHER_SRC="$ROOT/src" > /dev/null
cmake --build "$WORK/build_new" --target patch_corpus -j"$(nproc)" > /dev/null 2>&1

status=0
for RAW in "${RAWS[@]}"; do
   name="$(basename "$(dirname "$RAW")")_$(basename "$RAW")"
   echo
   echo "=== $RAW ==="
   rm -rf "$WORK/$name.base" "$WORK/$name.new"
   "$WORK/build_base/patch_corpus" "$RAW" "$WORK/$name.base" > /dev/null
   "$WORK/build_new/patch_corpus" "$RAW" "$WORK/$name.new" > /dev/null
   python3 "$HERE/../patched_diff.py" "$WORK/$name.base" "$WORK/$name.new" "$@" || status=1
done
exit $status
