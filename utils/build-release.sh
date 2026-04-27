#!/bin/bash

set -x -euo pipefail

DIST=dist
STAGING=_staging_
DATE=$(date +'%Y-%m-%d')
VERSION=$1
APC_MEMORY=${APC_MEMORY:-8192}

rm -f MyC64-Pocket.zip
rm -rf ${STAGING}

pushd src/bios
make clean
make
popd

if command -v apc >/dev/null 2>&1; then
  apc --memory "${APC_MEMORY}" --clean .
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
