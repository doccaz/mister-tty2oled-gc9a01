#!/usr/bin/env bash
# Rebuilds library/converted/ (and refreshes the web app's static copy)
# from the community marquee packs bundled in reference/Pictures/ZIPs.
# Requires: unrar, unzip, python3 + Pillow.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZIPS_DIR="${ROOT}/reference/Pictures/ZIPs"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

echo "Extracting archives from ${ZIPS_DIR} ..."
cd "${ZIPS_DIR}"
for f in *.rar; do
  [ -e "$f" ] || continue
  name="${f%.rar}"
  mkdir -p "${WORK_DIR}/extracted/${name}"
  unrar x -y -o+ "$f" "${WORK_DIR}/extracted/${name}/" >/dev/null
done
for f in *.zip; do
  [ -e "$f" ] || continue
  name="${f%.zip}"
  mkdir -p "${WORK_DIR}/extracted/${name}"
  unzip -o -q "$f" -d "${WORK_DIR}/extracted/${name}/"
done

echo "Converting .gsc/.xbm -> PNG ..."
rm -rf "${ROOT}/library/converted"
mkdir -p "${ROOT}/library/converted"
python3 "${ROOT}/tools/convert_library.py" "${WORK_DIR}/extracted" "${ROOT}/library/converted"

echo "Syncing into web/public/library ..."
rm -rf "${ROOT}/web/public/library"
mkdir -p "${ROOT}/web/public/library"
cp -r "${ROOT}/library/converted/." "${ROOT}/web/public/library/"

echo "Done. $(find "${ROOT}/library/converted" -name '*.png' | wc -l) images available."
