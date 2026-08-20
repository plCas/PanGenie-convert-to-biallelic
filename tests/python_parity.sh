#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
DATA="${ROOT}/tests/data/python_parity"
TMPDIR="${TMPDIR:-/tmp}/ctb_python_parity.$$"
mkdir -p "${TMPDIR}"
trap 'rm -rf "${TMPDIR}"' EXIT

gzip -c "${DATA}/annotation.vcf" > "${TMPDIR}/annotation.vcf.gz"

PYTHONHASHSEED=0 python3 "${ROOT}/convert-to-biallelic.py" \
  "${TMPDIR}/annotation.vcf.gz" \
  < "${DATA}/input.vcf" \
  > "${TMPDIR}/python.vcf"

"${ROOT}/build/convert-to-biallelic" \
  --variants "${TMPDIR}/annotation.vcf.gz" \
  --input "${DATA}/input.vcf" \
  --output "${TMPDIR}/cpp.vcf.gz" \
  --threads 4 \
  --memory-limit 256M \
  --quiet \
  --force \
  --compatibility python

gzip -dc "${TMPDIR}/cpp.vcf.gz" > "${TMPDIR}/cpp.vcf"
diff -u "${TMPDIR}/python.vcf" "${TMPDIR}/cpp.vcf"
