# convert_to_biallelic

`convert_to_biallelic` is an authored, source-only C++17/HTSlib converter for
transforming multiallelic VCF input into biallelic VCF output using an
annotation VCF.

## Build with CMake

This project is built with CMake and links against HTSlib through
`pkg-config`. `CMakeLists.txt` defaults to the local HTSlib installation at
`/../htslib-1.16`, so run:

```bash
cd /../Pangenie_to_biallelic
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The compiled executable is written to:

```text
/../PanGenie-convert-to-biallelic/build/convert-to-biallelic
```

To use a different HTSlib installation, pass its prefix explicitly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHTSLIB_ROOT=/path/to/htslib
```

## Intended usage

```text
convert-to-biallelic \
  --variants annotation.vcf.gz \
  --input multiallelic.vcf.gz \
  --output biallelic.vcf.gz \
  --threads 8 \
  --memory-limit 2G
```

Use Python compatibility mode when the output should match the local
`convert-to-biallelic.py` oracle for deterministic records:

```text
convert-to-biallelic \
  --variants annotation.vcf.gz \
  --input multiallelic.vcf \
  --output biallelic.vcf.gz \
  --threads 8 \
  --memory-limit 2G \
  --compatibility python
```

The default compatibility mode is `strict`, which preserves the authored C++
behavior. `--compatibility python` is opt-in and is intended to match
`convert-to-biallelic.py` output for deterministic records while keeping the
C++ streaming and ordered worker pipeline.

The intended executable requires explicit input, annotation, and output paths.
`--progress-interval` accepts whole seconds and defaults to 5; zero disables
periodic reports but not the final summary. It writes periodic progress and a
final peak-tracked-memory summary to stdout and diagnostics to stderr. Output is
written through a duplicated descriptor for an exclusively created native
file identity retained by the transaction; main never reopens the temporary
pathname for sink I/O. The requested path is published only after the pipeline
and output close succeed. Existing output is rejected unless `--force` is
supplied, and no-force publication does not replace a destination created
concurrently. The final success summary is intended to appear only after
publication succeeds, and `--quiet` suppresses both periodic progress and that
summary.

The authored soft-memory accounting temporarily admits both the existing and
requested new allocations while a pipeline-owned string or vector grows with
`reserve()`, then reconciles the implementation-selected capacity. The
separate 10% process reserve remains for allocator overhead, HTSlib, thread
stacks, and other untracked transients. This behavior remains runtime-unverified.

In strict mode, records that emit multiple distinct IDs at one annotation
position use a deterministic position-then-ID order. In Python compatibility
mode, ordering follows the Python oracle's coordinate-only sort where
deterministic.

The Python oracle collects variant IDs in a `set` and sorts only by coordinate.
For multiple distinct IDs at the same coordinate, Python output order can
depend on `PYTHONHASHSEED`. The parity test fixes `PYTHONHASHSEED=0`;
production byte identity for equal-position ties is therefore not guaranteed
unless the oracle order is also fixed.

The Linux implementation is intentionally Linux-specific. It prefers an
unnamed `O_TMPFILE` and publishes that retained identity with `AT_EMPTY_PATH`
when permitted, or through the documented unprivileged
`/proc/self/fd/<fd>` plus `AT_SYMLINK_FOLLOW` route. If the filesystem or
kernel does not support `O_TMPFILE`, it keeps an exclusively created named
temporary entry and verifies its device/inode identity against the retained
descriptor immediately before link/rename publication.

The transaction guarantee assumes normal, non-adversarial operation. It covers
failures, temporary-name collisions, and concurrent creation of the final
destination, but the output directory must not be modified by an adversarial
process while a transaction is active. Windows forced `MoveFileExW` and Linux
named-temporary or anonymous-stage fallbacks are pathname-based and are not
hardened against same-directory substitution between the best-effort identity
check and publication. These routes remain source-only and must be verified on
target kernels, filesystems, procfs mounts, and Windows installations.

See [UNVERIFIED.md](UNVERIFIED.md) for the remaining verification boundary.
