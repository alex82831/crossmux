#!/usr/bin/env bash
# Builds every app under sdk/dynapp/apps, publishes the .eapp files into
# store/, and regenerates store/catalog.json from what was actually built.
#
# The catalog is derived, never hand-edited: each app carries its own
# app.meta (name / version / note) and the byte count comes from the file on
# disk, so a stale size can no longer ship.
#
#   ./build-store.sh                 # build everything
#   ./build-store.sh aichat aidict   # build just these, refresh the catalog
#
# CROSSMUX_STORE_BASE overrides the download prefix baked into catalog.json.
set -euo pipefail

SDK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SDK_DIR/../.." && pwd)"
STORE="$REPO_DIR/store"
BASE="${CROSSMUX_STORE_BASE:-https://raw.githubusercontent.com/alex82831/crossmux/claude/project-research-firmware-build-xb63d3/store}"

mkdir -p "$STORE"

if [ $# -gt 0 ]; then
  APPS=("$@")
else
  APPS=()
  for d in "$SDK_DIR"/apps/*/; do APPS+=("$(basename "$d")"); done
fi

for app in "${APPS[@]}"; do
  "$SDK_DIR/build-eapp.sh" "$SDK_DIR/apps/$app" "$STORE/$app.eapp" > /dev/null
  printf '  %-12s %8d bytes\n' "$app" "$(stat -c%s "$STORE/$app.eapp")"
done

BASE="$BASE" python3 - "$SDK_DIR" "$STORE" <<'PY'
import json, os, pathlib, sys

sdk, store = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
base = os.environ["BASE"].rstrip("/")

apps = []
for meta_path in sorted(sdk.glob("apps/*/app.meta")):
    slug = meta_path.parent.name
    eapp = store / f"{slug}.eapp"
    if not eapp.exists():
        print(f"  skip {slug}: no built .eapp in store/", file=sys.stderr)
        continue
    meta = {}
    for line in meta_path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            meta[k.strip()] = v.strip()
    apps.append({
        "slug": slug,
        "name": meta.get("name", slug),
        "version": meta.get("version", "1.0.0"),
        "bytes": eapp.stat().st_size,
        "note": meta.get("note", ""),
        "url": f"{base}/{slug}.eapp",
    })

out = store / "catalog.json"
out.write_text(json.dumps({"apps": apps}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"catalog: {len(apps)} apps -> {out}")
PY
