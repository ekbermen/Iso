#!/bin/sh
# Gerekenler: pspdev (psp-gcc), mkisofs (cdrtools/genisoimage)
set -e
make clean; make
rm -rf iso; mkdir -p iso/PSP_GAME/SYSDIR
cp MiniArcade.prx iso/PSP_GAME/SYSDIR/EBOOT.BIN
cp PARAM.SFO iso/PSP_GAME/PARAM.SFO
mkisofs -iso-level 4 -xa -A "MINIARCADE" -V "MINIARCADE" -sysid "PSP GAME" -volset "MINIARCADE" -p "" -publisher "" -o MiniArcade.iso iso
echo "Hazir: MiniArcade.iso"
