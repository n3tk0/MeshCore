#!/usr/bin/env bash
# Fills the board drop-downs of .github/workflows/build-boards.yml from platformio.ini.
# Run it after adding boards or merging upstream:  bash tools/gen-board-choices.sh
set -e
cd "$(dirname "$0")/.."
WF=.github/workflows/build-boards.yml
tmp=$(mktemp)
cp "$WF" "$tmp"
for kind in repeater companion room-server; do
  list=$(bash build.sh get-$kind-firmwares-to-build | sort -f)
  # replace the lines between "# <kind> boards begin" and "# <kind> boards end"
  awk -v kind="$kind" -v list="$list" '
    $0 ~ "# " kind " boards begin" { print; ind = $0; sub(/#.*/, "", ind); n = split(list, a, "\n");
      for (i = 1; i <= n; i++) if (a[i] != "") print ind "- " a[i]; skip = 1; next }
    $0 ~ "# " kind " boards end" { skip = 0 }
    !skip { print }' "$tmp" > "$tmp.new"
  mv "$tmp.new" "$tmp"
done
mv "$tmp" "$WF"
echo "updated $WF"
