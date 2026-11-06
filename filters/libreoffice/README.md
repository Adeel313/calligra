# Development reference tools

Calligra's DOCX, XLSX and PPTX filters are native. They do not link to or invoke
LibreOffice, LibreOfficeKit, ONLYOFFICE or a conversion worker. LibreOffice is
not required to open, edit, save or export these formats.

This directory retains a development-only reference worker and compatibility
runners. The worker is excluded from the default build and is not installed.
Build it explicitly when running backend contract/error-handling tests:

```sh
cmake --build build --target calligraofficeconverter
```

LibreOffice is used only by reference tests that explicitly request it. The
native filter tests also exercise conversion with the backend unavailable.
The native presentation preprocessing code lives in `filters/libmsooxml/pptx`;
it has no reference-worker or LibreOffice dependency.

For document comparisons and artifacts, see `devtools/office/README.md` and
`filters/stage/pptx/README.md`. Matching the reference on a finite fixture set
does not establish support for every feature or exact pixel identity.
