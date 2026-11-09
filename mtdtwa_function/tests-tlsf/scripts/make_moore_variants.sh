#!/bin/bash
# Copy a tree of TLSF files, switching every spec to Moore semantics:
#   SEMANTICS: Mealy        -> Moore
#   SEMANTICS: Strict,Mealy -> Strict,Moore
#   TARGET:    Mealy        -> Moore
# Used to test Moore semantics at scale, since SYNTCOMP is almost all Mealy.
# //STATUS tags are removed: they are answers for the Mealy version.
# (Moore is harder for the system, so a Mealy-unrealizable spec stays
# unrealizable; a Mealy-realizable one may become unrealizable.)
#
# Usage: make_moore_variants.sh SRC_DIR DST_DIR

set -eu
[ $# -eq 2 ] || { echo "Usage: $0 SRC_DIR DST_DIR" >&2; exit 2; }
src=${1%/}; dst=${2%/}
[ -d "$src" ] || { echo "not a directory: $src" >&2; exit 2; }

n=0
while IFS= read -r f; do
  rel=${f#"$src"/}
  mkdir -p "$dst/$(dirname "$rel")"
  sed -E -e 's/(SEMANTICS:[[:space:]]*(Strict,)?)Mealy/\1Moore/' \
         -e 's/(TARGET:[[:space:]]*)Mealy/\1Moore/' \
         -e '/^[[:space:]]*\/\/[[:space:]]*STATUS[[:space:]]*:/d' \
         "$f" > "$dst/$rel"
  n=$((n + 1))
done < <(find "$src" -name '*.tlsf' -type f | sort)
echo "Wrote $n Moore variants to $dst"
