#!/bin/bash
# Run tlsf_runner (MTDSWA vs. ltlsynt) on many TLSF files in parallel, each
# under a time and memory limit, and summarize the results.
#
# Usage: run_suite.sh [-j JOBS] [-t SECS] [-m MB] [-o OUTDIR] [-r] PATH...
#   PATH     .tlsf files or directories (searched recursively)
#   -j JOBS  parallel runs (default 4).  Keep JOBS * MB below the RAM size.
#   -t SECS  time limit per file, translation + MTDSWA + ltlsynt (default 120)
#   -m MB    virtual memory limit per file (default 2500)
#   -o DIR   output directory (default tlsf_results); gets results.tsv,
#            summary.txt and logs/<file>.log
#   -r       require every file to MATCH (for sanity suites): timeouts and
#            inconclusive results also count as failures
#
# Exit code: 0 = no failures, 1 = a MISMATCH with ltlsynt, an MTDSWA
# verdict that contradicts the file's //STATUS tag, or (with -r) any file
# that did not MATCH.
#
# Environment: TLSF_RUNNER overrides the path of the tlsf_runner binary.

set -u
here=$(cd "$(dirname "$0")" && pwd)
RUNNER=${TLSF_RUNNER:-$here/../../tlsf_runner}
JOBS=4; TMO=120; MEM=2500; OUT=tlsf_results; REQUIRE_ALL=0

usage() { sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
while getopts "j:t:m:o:rh" opt; do
  case $opt in
    j) JOBS=$OPTARG ;; t) TMO=$OPTARG ;; m) MEM=$OPTARG ;;
    o) OUT=$OPTARG ;; r) REQUIRE_ALL=1 ;; *) usage ;;
  esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || usage
[ -x "$RUNNER" ] || { echo "tlsf_runner not found at $RUNNER (run 'make tlsf_runner')" >&2; exit 2; }

mkdir -p "$OUT/logs"
results=$OUT/results.tsv
printf 'file\tsemantics\tstates\tmtdswa\tltlsynt\tresult\texit\tsecs\texpected\n' > "$results"

# Build "NAME<TAB>FILE" pairs.  NAME is the path relative to the directory
# argument (or the file name for file arguments), without .tlsf.
list=$(mktemp)
trap 'rm -f "$list"' EXIT
for p in "$@"; do
  if [ -d "$p" ]; then
    find "$p" -name '*.tlsf' -type f | sort | while IFS= read -r f; do
      n=${f#"${p%/}"/}; printf '%s\t%s\n' "${n%.tlsf}" "$f"
    done
  elif [ -f "$p" ]; then
    n=$(basename "$p"); printf '%s\t%s\n' "${n%.tlsf}" "$p"
  else
    echo "warning: skipping $p (not a file or directory)" >&2
  fi
done > "$list"
total=$(wc -l < "$list")
echo "Running $total files, $JOBS in parallel, ${TMO}s / ${MEM}MB each -> $OUT"

# Simple job pool; each job appends one row (short appends are atomic).
while IFS=$'\t' read -r name file; do
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
  "$here/run_one.sh" "$RUNNER" "$OUT/logs" "$TMO" "$MEM" "$name" "$file" >> "$results" &
done < "$list"
wait

# Summary.
{
  echo "Files: $total"
  echo "Results:"
  tail -n +2 "$results" | cut -f6 | sort | uniq -c
  echo "MTDSWA verdicts on MATCH (Yes = realizable):"
  tail -n +2 "$results" | awk -F'\t' '$6=="MATCH"{print $4}' | sort | uniq -c
  echo "MISMATCH with ltlsynt:"
  tail -n +2 "$results" | awk -F'\t' '$6=="MISMATCH"{print "  "$1"  mtdswa="$4"  ltlsynt="$5}'
  echo "MTDSWA vs //STATUS tag (files that have one):"
  tail -n +2 "$results" | awk -F'\t' '$9!="-" && ($4=="Yes"||$4=="No") {
      got = ($4=="Yes") ? "realizable" : "unrealizable"
      if (got == $9) agree++; else { disagree++; bad = bad "  " $1 "  mtdswa=" got "  expected=" $9 "\n" } }
      END { printf "  agree: %d  disagree: %d\n%s", agree, disagree, bad }'
} | tee "$OUT/summary.txt"

fail=0
tail -n +2 "$results" | awk -F'\t' '$6=="MISMATCH"' | grep -q . && fail=1
tail -n +2 "$results" | awk -F'\t' '$9!="-" && ($4=="Yes"||$4=="No") &&
    (($4=="Yes") ? "realizable" : "unrealizable") != $9' | grep -q . && fail=1
if [ $REQUIRE_ALL -eq 1 ]; then
  tail -n +2 "$results" | awk -F'\t' '$6!="MATCH"' | grep -q . && fail=1
fi
[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
