# Ribbon command icons

These icons are the command symbols of the calligra ribbon (Sheets, Words and
Stage).

- Set: Material Symbols (Outlined, weight 400, 24px grid)
- Upstream: https://github.com/google/material-design-icons
- Package: `@material-symbols/svg-400` 0.28.0, `outlined/` SVGs taken unmodified
- License: Apache License, Version 2.0 (see `LICENSES/Apache-2.0.txt`)
- Copyright: Google LLC

The files keep their upstream names, so a command can refer to one by name.
They are installed to `$DATADIR/calligra/icons/ribbon/` and loaded by
`KoRibbonIcons` (`libs/widgets/`), which tints them per command family and
falls back to the desktop icon theme when a file is missing. Both the ribbon
and the tool panels of Sheets, Words and Stage use it.

To use a different file for a command, change the name in the command table of
`KoRibbonIcons.cpp` in `libs/widgets/`.
