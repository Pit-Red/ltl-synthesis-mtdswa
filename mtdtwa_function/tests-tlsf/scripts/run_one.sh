#!/bin/bash
# Run tlsf_runner on one TLSF file under a time and memory limit and print
# one tab-separated result row.  Called by run_suite.sh; not meant to be
# used directly.
#
# Usage: run_one.sh RUNNER LOGDIR TIMEOUT_SECS MEM_MB NAME FILE
#
# Output columns:
#   file semantics states mtdswa ltlsynt result exit secs expected
# result is MATCH / MISMATCH / INCONCLUSIVE from tlsf_runner, or
# RUNNER_TIMEOUT / CRASH_OR_OOM / NO_RESULT if the runner was killed.
# expected is the //STATUS tag of the file (SYNTCOMP convention), or "-".

RUNNER=$1; LOGDIR=$2; TMO=$3; MEM_MB=$4; name=$5; f=$6

log=$LOGDIR/$(printf '%s' "$name" | sed 's|/|__|g').log
start=$(date +%s.%N)
# ulimit -v caps virtual memory, so one huge automaton cannot take down
# the machine.  The ltlsynt reference gets its own time limit (half of
# ours) so that a slow reference still leaves time to report.
( ulimit -v $((MEM_MB * 1024))
  timeout "$TMO" "$RUNNER" "$f" --ltlsynt-timeout=$((TMO / 2)) ) > "$log" 2>&1
rc=$?
secs=$(echo "$(date +%s.%N) - $start" | bc)

field() { grep -m1 "^$1" "$log" | sed "s/^$1//"; }
sem=$(field 'Semantics: ' | sed 's/ (.*//')
states=$(field 'States: ')
swa=$(field 'Realizable (mtdswa): ')
ref=$(field 'Realizable (ltlsynt): ')
res=$(field 'RESULT: ')
expected=$(grep -m1 -i '^//STATUS *:' "$f" | sed 's/.*: *//' | tr -d '[:space:]')

if [ -z "$res" ]; then
  case $rc in
    124)         res=RUNNER_TIMEOUT ;;
    134|137|139) res=CRASH_OR_OOM ;;
    *)           res=NO_RESULT ;;
  esac
fi

printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%.1f\t%s\n' \
  "$name" "${sem:--}" "${states:--}" "${swa:--}" "${ref:--}" "$res" "$rc" "$secs" "${expected:--}"
