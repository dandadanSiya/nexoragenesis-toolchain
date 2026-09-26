#!/usr/bin/env bash
# Builds Nexora OS with the project's own NTASM compiler.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out="$here/build"
root="$repo/bootstrap/quarantine/c"
mkdir -p "$out/esp/EFI/BOOT"
${CC:-gcc} -std=c11 -O2 -I"$root" \
  "$root"/{main,cli,ntasm,frontend,codegen,x64,pe,object,object_build,object_link,object_materialize,conformance}.c \
  -o "$out/ntasm"
python3 "$here/tools/gen_assets.py" "$out/assets.ntasm"
python3 "$here/tools/ntpp.py" "$here/src/nexoros.ntasm" "$out/nexoros.ntasm"
rm -f "$out/esp/EFI/BOOT/BOOTX64.EFI"
"$out/ntasm" assemble "$out/nexoros.ntasm" -o "$out/esp/EFI/BOOT/BOOTX64.EFI"
echo "OK: $out/esp/EFI/BOOT/BOOTX64.EFI"
