#!/usr/bin/env bash
# Crosshair blend-mode check: GPU shaders vs Krita's own blend code.
#   bash tools/chblend_test/run.sh            (from the project root)
# Needs: MSVC 2022 (vcvars64), a D3D9 capable GPU, curl on the first run.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
FEAT="$ROOT/src/features"
KRITA_SHA=428d44705de20770434ea2915021241dc073879c   # KDE/krita master, 2026-09-16
KDIR="$HERE/.krita-$KRITA_SHA"
OUT="$HERE/.build"

if [ ! -f "$KDIR/.ok" ]; then
    mkdir -p "$KDIR"
    B="https://raw.githubusercontent.com/KDE/krita/$KRITA_SHA/libs/pigment"
    for f in KoColorSpaceMaths.h KoColorSpaceMaths.cpp KoIntegerMaths.h KoLut.h lut.h \
             compositeops/KoCompositeOpFunctions.h compositeops/KoCompositeOpGenericFunctorBase.h \
             compositeops/KoCompositeOpClampPolicy.h; do
        curl -sfL -o "$KDIR/$(basename "$f")" "$B/$f"
    done
    touch "$KDIR/.ok"
fi

VCVARS="$(ls -d "/c/Program Files/Microsoft Visual Studio/"*/*/VC/Auxiliary/Build/vcvars64.bat 2>/dev/null | tail -1)"
[ -n "$VCVARS" ] || { echo "vcvars64.bat not found"; exit 1; }

mkdir -p "$OUT"
w() { cygpath -w "$1"; }
cat > "$OUT/build.bat" <<EOF
@echo off
call "$(w "$VCVARS")" >nul || exit /b 1
cd /d "$(w "$OUT")"
cl /nologo /std:c++17 /O2 /EHsc /W0 /DNOMINMAX ^
   /I"$(w "$HERE/stubs")" /I"$(w "$KDIR")" /I"$(w "$FEAT")" ^
   "$(w "$HERE/chblend_test.cpp")" /Fe:chblend_test.exe d3d9.lib user32.lib || exit /b 1
EOF
cmd //c "$(w "$OUT/build.bat")"
"$OUT/chblend_test.exe"
