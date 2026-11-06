# Native Office filter benchmark

Run from any directory, with python-docx, openpyxl, python-pptx and lxml installed:

```sh
/usr/bin/python3 devtools/office/benchmark.py \
  --output build/native-office-benchmark --repeat 3 --size 200
```

This exercises all six native filter directions through `calligraconverter`
and compares each with the existing isolated LibreOfficeKit reference runner.
ODF inputs are prepared with LibreOffice, outside the timed region. Each
conversion runs in a fresh process, sequentially. Times include process startup,
plugin loading and file I/O; they are not isolated parser timings. Samples,
median times, output sizes, errors and sentinel text checks are saved in
`report.json`, with conversion logs and documents beside it. Failed samples
invalidate the median rather than making a failing converter appear faster.
The native/reference ratio is below one when Calligra finishes sooner.

The sentinel check detects basic text loss only. It does not certify formatting,
formulas, charts, image positioning or visual fidelity. Use the existing richer
Words/Sheets and Stage compatibility runners in `filters/libreoffice/tests` and
`filters/stage/pptx/tests` for those comparisons. Some older negative tests in
those runners assume LibreOffice-backed filters and need interpretation when
running the native implementations.

The runner rejects an already-used output directory to avoid stale LibreOffice
locks. Metadata records the LibreOffice version, build type and installed filter
hashes. Use a dedicated output directory per run and avoid running other conversion
suites at the same time when collecting performance results. Record the build
type: the existing local build is Debug, so results are not release-performance
claims. The reference currently uses the repository runner's Linux LibreOffice
installation path and `/usr/bin/python3`.

For an existing PPTX, use the presentation runner rather than sentinel fixtures:

```sh
/usr/bin/python3 devtools/office/presentation_benchmark.py /path/to/deck.pptx \
  --output build/deck-evaluation-new --repeat 3
```

It records wall time, CPU time, peak RSS and output size for native and
LibreOffice import and export on the same inputs. Untimed checks include the
Stage compatibility harness, all slide renders, exported-PPTX reopening,
per-filter fidelity (including native export of a common LibreOffice ODP), ZIP
integrity, an editing attempt, and malformed-input/backend-independence tests.
Source SHA-256 is checked before and after. Per-slide pixel and foreground
similarities are diagnostic; inspect the saved images for local layout errors.
Features absent from the source do not establish compatibility coverage.

To isolate PPTX export from import differences, give both engines the same ODP:

```sh
/usr/bin/python3 devtools/office/compare_pptx_export.py input.odp \
  --output build/pptx-export-comparison-new
```

Add `--baseline older-native.pptx` only when that file was exported from the same
ODP. Both engines' exported PPTX files are independently reopened by LibreOffice
for PDF comparisons. This is a fidelity check, not a timing benchmark.

For the native DOCX export comparison, use the same ODT in both engines:

```sh
/usr/bin/python3 devtools/office/compare_docx_export.py input.odt \
  --output build/docx-export-comparison-new
```

An optional `--baseline older-native.docx` records before/after page scores.
The tool independently reopens both exported DOCX files in LibreOffice, checks
content and package integrity, and saves page images, differences and raw checks.
It is a fidelity comparison; use separate fresh-process runs for timing.
