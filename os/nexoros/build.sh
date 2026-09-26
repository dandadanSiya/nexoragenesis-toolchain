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
python3 "$here/../kart/tools/ntpp.py" /dev/null /dev/null 2>/dev/null || true
# build the embedded Nova app with the project's Nova compiler
novacc="$out/novacc"
${CC:-gcc} -std=c11 -O2 -I"$root" \
  "$root"/nova/cli.c "$root"/{nova,ntasm,frontend,codegen,x64,pe,object,object_build,object_link,object_materialize,conformance}.c \
  "$root"/nova/run_host.c "$root"/nova/runtime_v2.c -o "$novacc"
rm -f "$out/hello.efi"
"$novacc" build "$here/apps/hello.nova" -o "$out/hello.efi"
python3 "$here/tools/embed_nova.py" "$out/hello.efi" "$out/novaapp.ntasm"
python3 "$here/tools/ntpp.py" "$here/src/nexoros.ntasm" "$out/nexoros.ntasm"
rm -f "$out/esp/EFI/BOOT/BOOTX64.EFI"
"$out/ntasm" assemble "$out/nexoros.ntasm" -o "$out/esp/EFI/BOOT/BOOTX64.EFI"
echo "OK: $out/esp/EFI/BOOT/BOOTX64.EFI"
