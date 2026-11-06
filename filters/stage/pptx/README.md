# Stage PowerPoint import and export

Stage reads PPTX files with its native OOXML importer and writes PPTX packages
with its native exporter. These filters do not launch LibreOffice or require an
external office suite.

The exporter writes text and vector shapes as editable DrawingML objects,
embeds pictures as individual images, and writes bar, line, area, pie, scatter, and
doughnut charts as native DrawingML chart parts with cached series data.
Other unsupported complex objects are retained as pictures.

Build targets: `calligra_filter_pptx2odp` and `calligra_filter_odp2pptx`.
The compatibility scripts can use LibreOffice as an optional reference when
comparing output; that is a test aid and is not needed to import or export PPTX.

Tables are exported as editable DrawingML tables, including merged cells,
rich cell text, column widths, cell padding, solid fills and borders. Text
inside vector shapes and notes-body placeholders is retained. Chart series
are read from the chart data sets rather than the proxy model's unimplemented
Qt data interface; scatter charts retain numeric X/Y values, and horizontal
bar charts retain their direction.

The LibreOffice comparison runner checks table cell positions, merge spans,
text and bar-chart direction as well as the existing feature checks:

```sh
/usr/bin/python3 filters/stage/pptx/tests/compatibility.py \
  --build build --install destdir/usr --output build/pptx-compatibility-new --jobs 2
```

Use a fresh output directory for a clean run. The finite fixture suite does not
certify arbitrary presentations. Complex transition variants, animation timelines and embedded-media playback
are not comprehensively certified by these tests.

Picture export prioritizes the image payload over auxiliary text children.
LibreOffice-generated ODP commonly places an empty `text:p` inside `draw:image`;
that paragraph must not turn the picture into an empty DrawingML text shape.
The `pictures_libreoffice_odp` regression exercises this producer-specific path,
alongside the direct PPTX picture/crop/rotation case.

DrawingML package regressions cover root group child offsets and the four
cardinal linear-gradient directions. Native preparation preserves existing
relationship parts byte-for-byte during math-only normalization and does not
create a relationships part where one was absent. Ordinary slides use a streaming
namespace check before allocating a normalization DOM.

```sh
cmake --build build --target test_native_pptx_preparation
build/bin/test_native_pptx_preparation
/usr/bin/python3 filters/stage/pptx/tests/package_regressions.py \
  --output build/pptx-package-regressions-new
```

The Python regression requires the two built filters in the local `destdir`
installation and the `stage_pptx_roundtrip` target. It checks the package produced
by the real Stage load/save path, including masters and layouts.

Native ODP hyperlinks place `text:a` inside the formatted `text:span`.
LibreOffice Impress drops the link text when those elements are nested in the
opposite order. The regression checks link text, target and bold formatting
after LibreOffice reopens native ODP:

```sh
/usr/bin/python3 filters/stage/pptx/tests/hyperlink_regressions.py \
  --output build/pptx-hyperlink-regressions-new
```

Stage round-trip checks and cross-application ODP rendering measure different
things. LibreOffice can render native ODP gradients and text layout differently
from Stage even when the exported PPTX retains the slide's content. Compare
both paths before treating a passing structural suite as visual compatibility.

The native exporter now writes text-box padding, paragraph spacing and line
height, paragraph/list margins and hanging indents, tab stops, RTL paragraph
direction, list numbering style/start values, superscript/subscript baselines,
capitalization and character spacing. Non-list paragraphs explicitly suppress
inherited bullets. Solid and linear-gradient page backgrounds are written from
the page or its selected master background. Font defaults are resolved from the
text document instead of assigning every unformatted run an 18-point font.

The export-options regression uses a single common LibreOffice-generated ODP
for both exporters. It compares 17 formatting/opacity settings, permits at most two EMU
of floating-point rounding in lengths, and checks the reopened PPTX rendering:

```sh
/usr/bin/python3 filters/stage/pptx/tests/export_options_regressions.py \
  --output build/pptx-export-options-new
```

Use the export-only comparison tool to compare an existing ODP independently
of PPTX import. Both resulting PPTX files are reopened by LibreOffice before
rendering. An optional older native export of the same input supplies per-slide
before/after measurements:

```sh
/usr/bin/python3 devtools/office/compare_pptx_export.py input.odp \
  --baseline previous-native.pptx --output build/pptx-export-comparison-new
```

The tool records content, table/chart data, notes, hyperlinks, package integrity,
source SHA-256 and per-slide image differences. Its 0.92 per-slide rendering gate
is unchanged from the presentation benchmark and does not certify exact visual
identity or features absent from the input.

Axial opacity gradients from LibreOffice ODP are expanded into symmetric Qt
gradient stops before export. Gradient alpha values are emitted using QColor's
floating-point precision rather than reducing them to 8-bit alpha. This keeps
translucent fades from turning into opaque black rectangles. The same fixture
checks the five mirrored opacity stops, with two units of tolerance in OOXML's
100000-unit opacity range.

PPTX import preserves constant-color transparency ramps using standard ODF
color and opacity gradients. Stops are sorted, and opacity defaults to opaque
for each stop independently. The shared ODF style writer emits the opacity
resources referenced by these fills. Embedded table definitions take precedence
over presets with the same ID; themed table backgrounds resolve an interior
color stop with its luminance, saturation and linear-light tint/shade modifiers.
Office Math grouping characters and their surrounding delimiters survive the
editable-run fallback.

Run the focused import regressions after building/installing the native plugin:

```sh
cmake --build build --target calligra_filter_pptx2odp test_native_pptx_preparation calligraofficeconverter
build/bin/test_native_pptx_preparation
/usr/bin/python3 filters/stage/pptx/tests/import_fidelity_regressions.py \
  --output build/pptx-import-fidelity-new
/usr/bin/python3 filters/stage/pptx/tests/test_compatibility_features.py
```

The generated import fixture checks transparent gradient order, direction and
alpha, a customized preset table ID, transformed theme fills and source
immutability. `calligraofficeconverter` is an explicitly built reference tool;
the native import plugin does not invoke it.
