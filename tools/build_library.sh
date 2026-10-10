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

# Optional user-supplied full-color art (library/local-extra/*.png|jpg, named
# after the core id, e.g. PSX.png). Gitignored like the packs above, so
# third-party logos never land in the repo. Copied in as pack "local-extra".
EXTRA_DIR="${ROOT}/library/local-extra"
if compgen -G "${EXTRA_DIR}/*" >/dev/null; then
  echo "Merging local-extra color art ..."
  mkdir -p "${ROOT}/library/converted/local-extra"
  python3 - "${EXTRA_DIR}" "${ROOT}/library/converted" <<'PY'
import json, shutil, sys
from pathlib import Path
src, out = Path(sys.argv[1]), Path(sys.argv[2])
idx_path = out / "index.json"
idx = json.loads(idx_path.read_text())
for f in sorted(src.iterdir()):
    if f.suffix.lower() not in (".png", ".jpg", ".jpeg"):
        continue
    shutil.copy(f, out / "local-extra" / f.name)
    idx.append({"pack": "local-extra", "name": f.stem, "kind": "color", "png": f"local-extra/{f.name}"})
idx_path.write_text(json.dumps(idx, indent=2))
PY
fi

echo "Syncing into web/public/library ..."
rm -rf "${ROOT}/web/public/library"
mkdir -p "${ROOT}/web/public/library"
cp -r "${ROOT}/library/converted/." "${ROOT}/web/public/library/"

echo "Done. $(find "${ROOT}/library/converted" -name '*.png' | wc -l) images available."
