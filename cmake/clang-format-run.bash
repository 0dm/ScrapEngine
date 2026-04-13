#!/usr/bin/env bash
# In-place clang-format for tracked sources (excludes third_party/).
# Skips index entries whose files are missing on disk (e.g. mid-rename).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
args=()
while IFS= read -r f; do
  [[ -f "$f" ]] || continue
  args+=("$f")
done < <(git ls-files | grep -E '\.(h|hpp|c|cpp|mm|m)$' | grep -v '^third_party/' || true)
((${#args[@]} == 0)) && exit 0
clang-format -i "${args[@]}"
