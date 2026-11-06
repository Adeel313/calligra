# XLSX export precision regression

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
