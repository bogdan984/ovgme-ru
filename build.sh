#!/bin/sh
# Кросс-сборка OvGME под Windows (x86_64) с помощью MinGW-w64
set -e
CXX=${CXX:-x86_64-w64-mingw32-g++}
CC=${CC:-x86_64-w64-mingw32-gcc}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
OUT=${OUT:-bin}
mkdir -p "$OUT/obj"
INC="-Iinclude -Iinclude/thirdparty/miniz -Iinclude/thirdparty/pugixml -Iinclude/thirdparty/xxhash"
for f in main.cpp src/*.cpp src/thirdparty/pugixml/pugixml.cpp; do
  $CXX -std=c++11 -O2 $INC -c "$f" -o "$OUT/obj/$(basename "$f").o"
done
for f in src/thirdparty/miniz/miniz.c src/thirdparty/xxhash/xxhash.c; do
  $CC -O2 $INC -c "$f" -o "$OUT/obj/$(basename "$f").o"
done
$WINDRES -c 65001 -i resource.rc -o "$OUT/obj/resource.o"
$CXX -mwindows -s -static -o "$OUT/OvGME.exe" "$OUT"/obj/*.o \
  -lcomctl32 -lgdi32 -lshlwapi -lws2_32 -lcomdlg32 -lshell32 -lole32 -luuid
if command -v chmcmd >/dev/null 2>&1; then
  (cd help/ru && chmcmd ovgme.hhp >/dev/null) && cp help/ru/ovgme.chm "$OUT/ovgme.chm"
fi
if command -v makensis >/dev/null 2>&1 && [ -f "$OUT/ovgme.chm" ] && [ "$OUT" = bin ]; then
  mkdir -p dist && (cd installer && makensis -V2 ovgme.nsi) && echo "OK: dist/OvGME_RU_1.7.4_setup.exe"
fi
echo "OK: $OUT/OvGME.exe"
