#!/usr/bin/env bash
# Starts Nexora Kart in a QEMU window with the OVMF UEFI firmware.
#   sudo apt install qemu-system-x86 ovmf python3 gcc
#   os/kart/run.sh
# Options (environment): SOUND=1 routes the PC speaker to PulseAudio,
# QEMU_EXTRA="..." adds QEMU arguments (e.g. "-display gtk,zoom-to-fit=on").
# The kernel talks to the emulated hardware directly: keep it in QEMU.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
[ -f "$here/build/esp/EFI/BOOT/BOOTX64.EFI" ] || "$here/build.sh"

code=""
for candidate in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd \
                 /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2/x64/OVMF_CODE.fd \
                 /usr/share/edk2-ovmf/x64/OVMF_CODE.fd; do
  if [ -f "$candidate" ]; then code=$candidate; break; fi
done
if [ -z "$code" ]; then
  echo "OVMF introuvable : installe le paquet ovmf (ou edk2-ovmf)." >&2
  exit 1
fi
vars_template=${code/CODE/VARS}
vars="$here/build/OVMF_VARS.fd"
cp "$vars_template" "$vars"

machine="q35"
extra=()
if [ -w /dev/kvm ]; then extra+=(-enable-kvm); fi
if [ "${SOUND:-0}" = 1 ]; then
  extra+=(-audiodev pa,id=speaker)
  machine="q35,pcspk-audiodev=speaker"
fi
# shellcheck disable=SC2206
extra+=(${QEMU_EXTRA:-})

exec qemu-system-x86_64 -machine "$machine" -m 512 -net none -no-reboot \
  -drive if=pflash,format=raw,readonly=on,file="$code" \
  -drive if=pflash,format=raw,file="$vars" \
  -drive format=raw,file=fat:rw:"$here/build/esp" \
  -serial stdio "${extra[@]}"
