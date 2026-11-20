#!/bin/bash
# Regenerate the committed subset TTFs from the full upstream fonts.
#
# Requires pyftsubset from fonttools. The full .ttf files are not committed;
# download the same releases (Inter 3.19, JetBrains Mono 2.304) into this
# directory before running. The subset output is committed so normal builds do
# not need Python or fonttools.
set -e
cd "$(dirname "$0")"
for f in JetBrainsMono-Regular Inter-Regular Inter-Bold; do
  pyftsubset "$f.ttf" --output-file="$f-Subset.ttf" \
    --unicodes='U+0020-007E,U+00A0-00FF' --layout-features='' \
    --no-hinting --desubroutinize
  echo "$f ok"
done
ls -l *Subset.ttf
