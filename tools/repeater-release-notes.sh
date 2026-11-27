#!/usr/bin/env bash
# The body of the "Build Repeater Firmwares" release, on stdout:
#   tools/repeater-release-notes.sh <out-dir> <targets-file> <notes.md> <tag>
# - <out-dir>: the downloaded artifacts: the firmware files <env>-<version>.* and <env>.board.json (tools/board-info.sh)
# - <targets-file>: every env that was to be built, one per line
# - <notes.md>: the general explanation (docs/release-notes/repeater.md), printed first; its line
#   <!-- README:repeater --> becomes README.md's text between <!-- release-notes:repeater:start ... --> and
#   <!-- release-notes:repeater:end --> (headings one level down, relative links made absolute at <tag>),
#   so what the fork adds is written once, in README.md
# - <tag>: the release tag, for the heading
# Then a table of every board that built (title, env, platform, radio, description) with the Bulgarian
# descriptions folded below, and the boards that did not build (no file <env>-* in <out-dir>).
# GitHub's limit for a release body is 125000 characters: the result is cut to fit, with a note.
OUT=$1
TARGETS=$2
NOTES=$3
TAG=$4
[ -d "$OUT" ] && [ -f "$TARGETS" ] && [ -f "$NOTES" ] || { echo "usage: $0 <out-dir> <targets-file> <notes.md> <tag>" >&2; exit 2; }
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TSV=${BOARDS_TSV:-$ROOT/docs/release-notes/boards.tsv}
LIMIT=${BODY_LIMIT:-120000}
README=${README_MD:-$ROOT/README.md}
REPO=${GITHUB_REPOSITORY:-n3tk0/MeshCore}
REF=${TAG:-main}

# README.md's repeater part, ready for the release page
readme_part() {
  if ! grep -q '^<!-- release-notes:repeater:start' "$README" || ! grep -q '^<!-- release-notes:repeater:end' "$README"; then
    echo "repeater-release-notes: the release-notes:repeater markers are missing in $README" >&2
    return 1
  fi
  local base="https://github.com/$REPO/blob/$REF"
  awk '/^<!-- release-notes:repeater:end/ { on = 0 } on { print } /^<!-- release-notes:repeater:start/ { on = 1 }' "$README" |
    sed -E -e 's/^(#{2,5}) /#\1 /' \
           -e "s|\]\(\./|](${base}/|g" \
           -e "s|\]\(#|](${base}/README.md#|g"
}
part=$(readme_part) || exit 1
[ -n "$part" ] || { echo "repeater-release-notes: the repeater part of $README is empty" >&2; exit 1; }

built=()
failed=()
while IFS= read -r env; do
  [ -n "$env" ] || continue
  if compgen -G "$OUT/$env-*" >/dev/null; then built+=("$env"); else failed+=("$env"); fi
done < "$TARGETS"

# one JSON per built board (a board with files but no .board.json still gets a row, with its env as title)
infos=$(mktemp)
trap 'rm -f "$infos"' EXIT
for env in "${built[@]}"; do
  if [ -s "$OUT/$env.board.json" ] && jq -e . "$OUT/$env.board.json" >/dev/null 2>&1; then
    jq -c . "$OUT/$env.board.json"
  else
    jq -cn --arg e "$env" '{env: $e, title: $e, platform: "", radio: "", description: "", description_bg: ""}'
  fi
done > "$infos"

title_of() {   # the hand-written title of an env, else the env
  local t
  t=$(awk -F'\t' -v e="$1" '$1 == e && $2 != "" { print $2; exit }' "$TSV" 2>/dev/null)
  printf '%s' "${t:-$1}"
}

body() {
  while IFS= read -r line || [ -n "$line" ]; do
    if [ "$line" = "<!-- README:repeater -->" ]; then printf '%s\n' "$part"; else printf '%s\n' "$line"; fi
  done < "$NOTES"
  echo
  echo "---"
  echo
  echo "## Boards in this release ($TAG) / Платки в това издание"
  echo
  echo "${#built[@]} / $(grep -c . "$TARGETS") built / компилирани."
  echo
  echo "| Board | Env | Platform | Radio | Description |"
  echo "|---|---|---|---|---|"
  jq -rs 'sort_by([.platform, (.title | ascii_downcase)]) | .[]
          | "| \(.title | gsub("\\|"; "/")) | `\(.env)` | \(.platform) | \(.radio | gsub("\\|"; "/")) | \(.description | gsub("\\|"; "/")) |"' "$infos"
  if [ "${NO_BG_TABLE:-0}" != "1" ]; then
  echo
  echo "<details><summary>Описание на български</summary>"
  echo
  echo "| Платка | Описание |"
  echo "|---|---|"
  jq -rs 'sort_by([.platform, (.title | ascii_downcase)]) | .[]
          | "| \(.title | gsub("\\|"; "/")) (`\(.env)`) | \(.description_bg | gsub("\\|"; "/")) |"' "$infos"
  echo
  echo "</details>"
  fi
  if [ ${#failed[@]} -gt 0 ]; then
    echo
    echo "## Not built / Не са компилирани"
    echo
    echo "These boards failed to build in this run and have no files here / Тези платки не се компилираха и нямат файлове в това издание:"
    echo
    for env in "${failed[@]}"; do echo "- $(title_of "$env") (\`$env\`)"; done
  fi
}

text=$(body)
if [ ${#text} -gt "$LIMIT" ]; then
  # too long: first leave out the Bulgarian table (the app shows those descriptions from ota-manifest.json)
  text=$(NO_BG_TABLE=1 body)
fi
if [ ${#text} -gt "$LIMIT" ]; then
  # still too long: cut the board table, but keep the list of the boards that did not build
  tail_part=""
  if [ ${#failed[@]} -gt 0 ]; then tail_part=$'\n## Not built / Не са компилирани\n\n'$(for env in "${failed[@]}"; do echo "- $(title_of "$env") (\`$env\`)"; done); fi
  note=$'\n\n... (cut: the release text is too long, see ota-manifest.json for every board) / (съкратено, вижте ota-manifest.json)\n'
  keep=$((LIMIT - ${#tail_part} - ${#note}))
  text="${text:0:$keep}"
  text="${text%$'\n'*}$note$tail_part"
fi
printf '%s\n' "$text"
