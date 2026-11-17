# XLSX export precision regression

Check XLSX package content types (including absence of the ODF `mimetype`
entry, which causes Excel to report damaged content):

```sh
/usr/bin/python3 filters/sheets/xlsx/tests/package_content_types.py \
  --output build/xlsx-package-check
```

This uses the locally built and installed native exporter. The output directory
must be new; the generated ODS, XLSX and converter log are retained.
It also checks that whole-sheet column formatting stops at Excel's last column,
XFD (16,384), rather than Sheets' internal limit of 32,767.

Check that common and individually resized row heights survive export and
reimport:

```sh
/usr/bin/python3 filters/sheets/xlsx/tests/row_heights.py \
  --output build/xlsx-row-heights-check
```

Check borders on empty cells and covered cells along merged-cell edges
(requires openpyxl):

```sh
/usr/bin/python3 filters/sheets/xlsx/tests/empty_cell_borders.py \
  --output build/xlsx-empty-cell-borders-check
```

Run against the locally built and installed native XLSX exporter:

```sh
/usr/bin/python3 filters/sheets/xlsx/tests/number_formats.py \
  --output build/xlsx-precision-check-new
```

Requires openpyxl and the installed LibreOfficeKit reference runner. The output
folder must be new. Both exporters receive the same generated ODS; logs and both
XLSX files are retained. Checks cover integer formatting, four decimal places,
grouped numbers, percentages, scientific notation, repeated formats, and numeric
values. Equivalent optional leading digit placeholders are accepted. These are
format-code checks, not visual or comprehensive Office compatibility checks.

The shared ODF reader's zero-decimal regression is also covered by
`TestNumberStyle::testLoadNumberPrecision`.

# XLSX default-namespace drawing and chart import

```sh
/usr/bin/python3 filters/sheets/xlsx/tests/default_namespaces.py \
  --output build/xlsx-default-namespaces-check
```

Generates a workbook with openpyxl (which, like Excel, binds the
spreadsheetDrawing and chart namespaces as the default namespace instead of
using `xdr:`/`c:` prefixes), converts it with the native filter and checks that
the image, chart, merged title, formulas, values, date format and cell note reach
the ODS output. The note also checks that the comments part is found through the
sheet relationships rather than a hardcoded `xl/comments1.xml` name. It fails on
any element-not-found error in the converter log. This covers producers that omit
the conventional prefixes; prefixed input such as LibreOffice's is not generated
here.
