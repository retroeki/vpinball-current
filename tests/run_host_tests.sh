#!/usr/bin/env bash
# license:GPLv3+
#
# Every host-side regression suite of the Android fork, in one go (WSL/Linux, needs g++,
# cmake, python3). Run from anywhere:
#
#   tests/run_host_tests.sh
#
# Optional corpus gates (local libraries, not in the repo):
#   OPTIONS_TSV=options.tsv   F12 options of a corpus (tests/tableoptions/extract_options.py):
#                             only the options in tests/tableoptions/expected_changes.txt
#                             may change default on a phone
#   PATCHER_REV=HEAD RAW_CORPORA="dir1 dir2"
#                             patcher of the working tree vs REV over whole corpora; any
#                             changed table fails unless named in PATCHER_EXPECT="a b"
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cd "$ROOT"

pass=0
fail=0
run() {
   local name="$1"
   shift
   printf '%-46s' "$name"
   if "$@" > "$OUT/log" 2>&1; then
      echo "ok   $(grep -E 'ALL PASS|PASS:|passed|^PASS [0-9]|tables compared' "$OUT/log" | tail -1)"
      pass=$((pass + 1))
   else
      echo "FAIL"
      sed 's/^/      /' "$OUT/log" | grep -v '^      PASS' | tail -25
      fail=$((fail + 1))
   fi
}

run "table patches (tablepatches.cpp)" \
   sh -c "g++ -std=c++17 -Isrc tests/test-tablepatches.cpp src/ui/tablepatches.cpp -o $OUT/tablepatches && $OUT/tablepatches"
if [ -n "${OPTIONS_TSV:-}" ]; then
   run "F12 option phone defaults + corpus" \
      sh -c "g++ -std=c++17 -Isrc tests/test-tableoptiondefault.cpp -o $OUT/tod && $OUT/tod '$OPTIONS_TSV' tests/tableoptions/expected_changes.txt"
else
   run "F12 option phone defaults" \
      sh -c "g++ -std=c++17 -Isrc tests/test-tableoptiondefault.cpp -o $OUT/tod && $OUT/tod"
fi
run "score reel classifier (ReelDmd)" \
   sh -c "g++ -std=c++17 -Isrc tests/test-reel-classifier.cpp src/core/ReelClassifier.cpp -o $OUT/reels && $OUT/reels"
run "tournament lock" \
   sh -c "g++ -std=c++17 -Isrc tests/test-tournament-lock.cpp src/core/TournamentLock.cpp -o $OUT/tlock && $OUT/tlock"
run "PuP ScreenType + score screen (real packs)" \
   sh -c "g++ -std=c++20 -Iplugins/pup tests/pup/test-pup.cpp -o $OUT/pup && $OUT/pup tests/pup/fixtures"
run "ScoreView layout choice (1,399 ROMs)" \
   sh -c "g++ -std=c++20 -Iplugins/scoreview tests/scoreview/test-layoutchoice.cpp -o $OUT/tlc && $OUT/tlc plugins/scoreview/layouts tests/scoreview"
run "patcher rule: Controller.Pause in If" python3 tests/scriptpatcher/test_controller_pause_if.py
run "Wine VBScript cases (vbs_check)" tests/scriptpatcher/wine_harness/run.sh

if [ -n "${PATCHER_REV:-}" ] && [ -n "${RAW_CORPORA:-}" ]; then
   expect=()
   for e in ${PATCHER_EXPECT:-}; do expect+=(--expect "$e"); done
   # shellcheck disable=SC2086
   run "patcher diff vs $PATCHER_REV (corpora)" \
      tests/scriptpatcher/corpus_harness/patcher_diff.sh "$PATCHER_REV" "$HOME/patcher_diff" $RAW_CORPORA -- "${expect[@]}"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
