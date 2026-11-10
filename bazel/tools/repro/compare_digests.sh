#!/bin/bash
# Compare two cold_build.sh results: the image digest, then the output files that differ (path, both hashes).
set -euo pipefail
a=$1 b=$2
echo "image: $(cat "$a/image_digest.txt") vs $(cat "$b/image_digest.txt")"
join -j2 -o 0,1.1,2.1 <(awk '{print $1, $2}' "$a/outputs.sha256" | sort -k2) <(awk '{print $1, $2}' "$b/outputs.sha256" | sort -k2) \
  | awk '$2 != $3' > "$b/differs.txt"
echo "differing outputs: $(wc -l < "$b/differs.txt") of $(wc -l < "$a/outputs.sha256") (list: $b/differs.txt)"
echo "only in one run: $(join -v1 -v2 -j2 <(awk '{print $1, $2}' "$a/outputs.sha256" | sort -k2) <(awk '{print $1, $2}' "$b/outputs.sha256" | sort -k2) | wc -l)"
