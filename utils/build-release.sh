#!/bin/bash

set -x -euo pipefail

DIST=dist
STAGING=_staging_
DATE=$(date +'%Y-%m-%d')
VERSION=$1

rm -f MyC64-Pocket.zip
rm -rf ${STAGING}

pushd src/bios
make clean
make
popd

# The C64 machine is now imported from MiSTer RTL. Keep the retained 1541
# generated source refreshed when Amaranth is available, but allow apc-only
# builders to use the checked-in generated Verilog.
if python3 -c 'import amaranth' >/dev/null 2>&1; then
  pushd src/fpga/core/my1541-rtl
  python3 my1541.py
  popd
else
  test -f src/fpga/core/my1541-rtl/my1541.v
fi

if command -v apc >/dev/null 2>&1; then
  apc --clean .
else
  quartus_sh --flow compile ./src/fpga/ap_core.qpf
  python3 utils/reverse-bits.py src/fpga/output_files/ap_core.rbf \
    ${DIST}/Cores/markus-zzz.MyC64/bitstream.rbf_r
fi

cp -r ${DIST} ${STAGING}

python3 - "${STAGING}/Cores/markus-zzz.MyC64/core.json" "${VERSION}" "${DATE}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
text = text.replace("VERSION", sys.argv[2]).replace("DATE_RELEASE", sys.argv[3])
path.write_text(text)
PY

pushd ${STAGING}
zip -r ../MyC64-Pocket.zip .
popd

cat src/fpga/output_files/ap_core.fit.summary
