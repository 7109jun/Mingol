#!/bin/bash
# usage: ci-annotate.sh <title> <logfile>  — emits the interesting log lines as one error annotation.
t=$1; f=$2
{ grep -E 'Failed|FAIL|\.cpp:[0-9]+:|ThreadSanitizer|AddressSanitizer|ERROR|SUMMARY|\*\*\*|Timeout' "$f" | sort -u | head -45; echo '--- tail ---'; tail -8 "$f"; } \
  | sed -e 's/%/%25/g' -e ':a;N;$!ba;s/\n/%0A/g' | cut -c1-30000 | sed "s/^/::error title=$t::/"
