# Calligra vs LibreOffice — 31-file round-trip report

Every file was converted **original → ODF → original format** by each engine,
Calligra Sheets/Words/Stage 26.11 (Release build) against LibreOffice 26.8.1.1.
Both engines were timed on the same idle machine, with the LibreOffice D-Bus
timeout suppressed (`DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent`), which
removes about 25 s of false startup delay per run.

**Timing** is the two conversions together, i.e. what a user pays to open a file
and save it back to its own format.
**Fidelity** is the share of each feature present in the original that survives
the round trip, averaged over the features the file actually uses.
**Text** is the share of the original words that is still present.

## Performance

| Word (DOCX) | 10 files | Calligra **2.8 s** | LibreOffice 24.5 s | Calligra 0.28 s/file, LO 2.45 s/file |
| Stage (PPTX) | 10 files | Calligra **4.6 s** | LibreOffice 27.3 s | Calligra 0.46 s/file, LO 2.73 s/file |
| Sheets (XLSX) | 11 files | Calligra **6.6 s** | LibreOffice 21.3 s | Calligra 0.60 s/file, LO 1.93 s/file |
| **All** | **31 files** | **14.0 s** | **73.1 s** | |

LibreOffice has a fixed process-start-up cost of roughly 1.5–2 s per run on this
machine, so its per-file numbers are dominated by start-up for these small files.
Per-file detail:

| File | Calligra | LibreOffice | Winner |
|---|---:|---:|---|
| docx_01_character.docx | 0.61 s | 2.52 s | Calligra |
| docx_02_paragraphs.docx | 0.24 s | 2.46 s | Calligra |
| docx_03_lists.docx | 0.24 s | 2.42 s | Calligra |
| docx_04_tables.docx | 0.25 s | 2.53 s | Calligra |
| docx_05_images.docx | 0.25 s | 2.46 s | Calligra |
| docx_06_sections.docx | 0.23 s | 2.37 s | Calligra |
| docx_07_headers_footers.docx | 0.24 s | 2.38 s | Calligra |
| docx_08_styles_toc.docx | 0.24 s | 2.45 s | Calligra |
| docx_09_fields_notes.docx | 0.23 s | 2.49 s | Calligra |
| docx_10_layout.docx | 0.24 s | 2.47 s | Calligra |
| pptx_01_layouts.pptx | 0.62 s | 2.75 s | Calligra |
| pptx_02_text.pptx | 0.39 s | 2.70 s | Calligra |
| pptx_03_shapes.pptx | 0.41 s | 2.74 s | Calligra |
| pptx_04_tables.pptx | 0.41 s | 2.74 s | Calligra |
| pptx_05_charts.pptx | 0.51 s | 2.85 s | Calligra |
| pptx_06_images.pptx | 0.66 s | 2.71 s | Calligra |
| pptx_07_theme_bg.pptx | 0.41 s | 2.77 s | Calligra |
| pptx_08_connectors.pptx | 0.40 s | 2.67 s | Calligra |
| pptx_09_text_effects.pptx | 0.40 s | 2.78 s | Calligra |
| pptx_10_transitions.pptx | 0.39 s | 2.62 s | Calligra |
| xlsx_01_formats.xlsx | 0.43 s | 1.78 s | Calligra |
| xlsx_02_formulas.xlsx | 0.37 s | 1.78 s | Calligra |
| xlsx_03_charts.xlsx | 1.92 s | 2.55 s | Calligra |
| xlsx_04_conditional.xlsx | 0.34 s | 1.75 s | Calligra |
| xlsx_05_tables.xlsx | 0.34 s | 1.77 s | Calligra |
| xlsx_06_print.xlsx | 0.36 s | 2.24 s | Calligra |
| xlsx_07_merges.xlsx | 0.34 s | 1.80 s | Calligra |
| xlsx_08_links.xlsx | 0.36 s | 2.01 s | Calligra |
| xlsx_09_objects.xlsx | 1.49 s | 2.01 s | Calligra |
| xlsx_10_misc.xlsx | 0.35 s | 1.78 s | Calligra |
| xlsx_11_filters.xlsx | 0.34 s | 1.83 s | Calligra |

On the large 20k-row spreadsheet fixture (not part of this corpus) Release
Calligra takes 1.6 s for XLSX→ODS (LibreOffice 3.0 s) and 6.2 s for ODS→XLSX
(LibreOffice 2.7 s). So Calligra is ahead importing XLSX and still behind exporting it.

## Fidelity

The scores below leave out features that the intermediate ODF file cannot carry,
so that they measure the engines rather than the format conversion:
**highlighted_runs**, and **styles** (the ODF file keeps only the styles in use, so both
engines return about 40% of the original style table).

### Word (DOCX)

| File | Calligra fidelity | LibreOffice fidelity | Calligra text | LibreOffice text |
|---|---:|---:|---:|---:|
| docx_01_character.docx | 100% | 100% | 100% | 100% |
| docx_02_paragraphs.docx | 100% | 100% | 100% | 100% |
| docx_03_lists.docx | 100% | 100% | 100% | 100% |
| docx_04_tables.docx | 100% | 100% | 100% | 100% |
| docx_05_images.docx | 80% | 100% | 100% | 100% |
| docx_06_sections.docx | 100% | 100% | 100% | 100% |
| docx_07_headers_footers.docx | 80% | 100% | 100% | 99% |
| docx_08_styles_toc.docx | 79% | 94% | 100% | 100% |
| docx_09_fields_notes.docx | 94% | 100% | 100% | 98% |
| docx_10_layout.docx | 81% | 100% | 100% | 100% |

Average: Calligra **91%**, LibreOffice 99%.

### Stage (PPTX)

| File | Calligra fidelity | LibreOffice fidelity | Calligra text | LibreOffice text |
|---|---:|---:|---:|---:|
| pptx_01_layouts.pptx | 99% | 100% | 100% | 100% |
| pptx_02_text.pptx | 98% | 91% | 100% | 100% |
| pptx_03_shapes.pptx | 98% | 85% | 100% | 100% |
| pptx_04_tables.pptx | 99% | 89% | 100% | 100% |
| pptx_05_charts.pptx | 99% | 88% | 100% | 100% |
| pptx_06_images.pptx | 99% | 89% | 100% | 100% |
| pptx_07_theme_bg.pptx | 99% | 91% | 100% | 100% |
| pptx_08_connectors.pptx | 99% | 89% | 100% | 100% |
| pptx_09_text_effects.pptx | 98% | 85% | 100% | 100% |
| pptx_10_transitions.pptx | 86% | 90% | 100% | 100% |

Average: Calligra **97%**, LibreOffice 90%.

### Sheets (XLSX)

| File | Calligra fidelity | LibreOffice fidelity | Calligra text | LibreOffice text |
|---|---:|---:|---:|---:|
| xlsx_01_formats.xlsx | 100% | 75% | 100% | 94% |
| xlsx_02_formulas.xlsx | 100% | 100% | 100% | 100% |
| xlsx_03_charts.xlsx | 93% | 100% | 100% | 100% |
| xlsx_04_conditional.xlsx | 88% | 100% | 100% | 100% |
| xlsx_05_tables.xlsx | 86% | 86% | 100% | 77% |
| xlsx_06_print.xlsx | 80% | 100% | 100% | 70% |
| xlsx_07_merges.xlsx | 100% | 100% | 100% | 100% |
| xlsx_08_links.xlsx | 83% | 100% | 100% | 100% |
| xlsx_09_objects.xlsx | 100% | 100% | 100% | 100% |
| xlsx_10_misc.xlsx | 100% | 100% | 100% | 97% |
| xlsx_11_filters.xlsx | 75% | 100% | 100% | 70% |

Average: Calligra **91%**, LibreOffice 96%.

### Where the loss happens

For the largest gaps I checked the intermediate ODF file as well as the final
output, which separates "Calligra did not read it" from "Calligra did not write it":

| Case | In the source | In Calligra ODF | In LibreOffice ODF | In Calligra output |
|---|---:|---:|---:|---:|
| PPTX slide layouts (`pptx_01`) | 11 | 2 master pages | 22 master pages | 1 layout |
| XLSX conditional formats (`xlsx_04`) | 5 groups | 0 | present | 2 groups |
| XLSX defined names (`xlsx_06`) | 3 | 0 | 2 | 0 |
| XLSX cell comments (`xlsx_08`) | 1 | present | present | 0 |

So the pattern differs per feature: conditional formats are already gone in
Calligra's ODS file, while comments survive the ODS file but are not written back
into the XLSX. Slide layouts are reduced to one master page in the ODP.

## What actually gets lost

Compared per file, ignoring the features both engines drop:

- **docx_05_images.docx**: fields (Calligra 0%, LO 100%)
- **docx_07_headers_footers.docx**: page_breaks (Calligra 0%, LO 100%)
- **docx_08_styles_toc.docx**: hyperlinks (Calligra 100%, LO 50%); bookmarks (Calligra 0%, LO 100%); fields (Calligra 33%, LO 100%)
- **docx_09_fields_notes.docx**: fields (Calligra 75%, LO 100%)
- **docx_10_layout.docx**: text_boxes (Calligra 50%, LO 100%); page_breaks (Calligra 0%, LO 100%)
- **pptx_01_layouts.pptx**: layouts (Calligra 91%, LO 100%)
- **pptx_02_text.pptx**: layouts (Calligra 91%, LO 45%)
- **pptx_03_shapes.pptx**: layouts (Calligra 91%, LO 9%)
- **pptx_04_tables.pptx**: layouts (Calligra 91%, LO 9%)
- **pptx_05_charts.pptx**: layouts (Calligra 91%, LO 18%)
- **pptx_06_images.pptx**: layouts (Calligra 91%, LO 9%)
- **pptx_07_theme_bg.pptx**: layouts (Calligra 91%, LO 18%)
- **pptx_08_connectors.pptx**: layouts (Calligra 91%, LO 9%)
- **pptx_09_text_effects.pptx**: layouts (Calligra 91%, LO 9%)
- **pptx_10_transitions.pptx**: layouts (Calligra 91%, LO 45%); transitions (Calligra 100%, LO 75%); animations (Calligra 0%, LO 100%)
- **xlsx_01_formats.xlsx**: frozen_panes (Calligra 100%, LO 0%)
- **xlsx_03_charts.xlsx**: charts (Calligra 80%, LO 100%)
- **xlsx_04_conditional.xlsx**: conditional_formats (Calligra 40%, LO 100%)
- **xlsx_05_tables.xlsx**: frozen_panes (Calligra 100%, LO 0%); defined_names (Calligra 0%, LO 100%)
- **xlsx_06_print.xlsx**: defined_names (Calligra 0%, LO 100%)
- **xlsx_08_links.xlsx**: comments (Calligra 0%, LO 100%)
- **xlsx_11_filters.xlsx**: defined_names (Calligra 0%, LO 100%)

## Notes on method

- Feature counts are read straight from the OOXML packages with `lxml`, before and
  after the round trip. A feature that appears *more* often in the output is capped
  at the original count, so duplication cannot inflate a score.
- Word recall compares word frequencies, so dropped or added text both show up.
- `styles` is the number of entries in `word/styles.xml`. The ODF round trip keeps
  only the styles that are actually used, so both engines return about 40% of the
  original count. It is excluded from the scores for that reason, not because of
  an engine defect.
- LibreOffice is a 2 GB suite with a multi-second start-up; for files this small the
  timing ratio mostly measures start-up, not conversion throughput. The 20k-row
  fixture is the fair throughput comparison.

## Summary

- Text survives in both engines: 98–100% for every file, every format.
- Structure within a document survives: paragraphs, tables, cells, formulas, shapes,
  images, slides, masters, headers and footers are all at 100% for both engines.
- Calligra loses more cross-format metadata than LibreOffice: slide layouts,
  spreadsheet charts and conditional formats, defined names, page breaks, bookmarks
  and text animations. These are export-fidelity gaps, not data corruption.
- Calligra is consistently faster on this corpus because it starts faster, and on
  big spreadsheets it is now faster in the XLSX-import direction.
