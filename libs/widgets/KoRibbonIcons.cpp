/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "KoRibbonIcons.h"

#include <QApplication>
#include <QColor>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStandardPaths>

namespace
{
// The ribbon ships its own command icons (Material Symbols, Apache-2.0) so the
// tabs do not depend on the desktop icon theme carrying a symbol for every
// spreadsheet, text or presentation command.
QIcon bundledIcon(const QString &name)
{
    static const QString directory = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                            QStringLiteral("calligra/icons/ribbon"),
                                                            QStandardPaths::LocateDirectory);
    if (directory.isEmpty() || name.isEmpty()) {
        return QIcon();
    }
    const QIcon icon(directory + QLatin1Char('/') + name + QStringLiteral(".svg"));
    return icon.isNull() ? QIcon() : icon;
}

// Sheets icons are single-colour Material symbols. Give each command family an
// accent colour so the ribbon reads like a colour-coded toolbar instead of a
// wall of grey outlines.
QColor iconTint(const QString &name)
{
    // Function library categories keep their own accent each.
    if (name == QLatin1String("text_fields")) {
        return QColor(0x1a, 0x73, 0xe8);
    }
    if (name == QLatin1String("calendar_month")) {
        return QColor(0x00, 0x89, 0x7b);
    }
    if (name == QLatin1String("calculate")) {
        return QColor(0xe8, 0x71, 0x0a);
    }
    if (name == QLatin1String("manage_search")) {
        return QColor(0x1a, 0x73, 0xe8);
    }
    if (name.startsWith(QLatin1String("format_align")) || name.startsWith(QLatin1String("vertical_align"))
        || name.startsWith(QLatin1String("format_indent")) || name == QLatin1String("wrap_text")
        || name == QLatin1String("text_rotation_angleup") || name == QLatin1String("text_rotate_vertical")) {
        return QColor(0x1a, 0x73, 0xe8); // alignment: blue
    }
    if (name == QLatin1String("numbers") || name == QLatin1String("percent") || name == QLatin1String("attach_money")
        || name.startsWith(QLatin1String("decimal_")) || name == QLatin1String("text_increase")
        || name == QLatin1String("text_decrease")) {
        return QColor(0x0f, 0x9d, 0x58); // numbers: green
    }
    if (name.startsWith(QLatin1String("border")) || name == QLatin1String("cell_merge")
        || name == QLatin1String("combine_columns") || name == QLatin1String("call_split")) {
        return QColor(0x00, 0x89, 0x7b); // borders and merges: teal
    }
    if (name == QLatin1String("format_color_fill") || name == QLatin1String("format_color_text")
        || name == QLatin1String("format_paint") || name == QLatin1String("style") || name == QLatin1String("table_chart")
        || name == QLatin1String("dashboard_customize")) {
        return QColor(0xe8, 0x71, 0x0a); // styles: orange
    }
    if (name == QLatin1String("sort") || name == QLatin1String("filter_alt") || name == QLatin1String("filter_list")
        || name == QLatin1String("splitscreen") || name == QLatin1String("fact_check") || name == QLatin1String("merge")
        || name == QLatin1String("pivot_table_chart") || name == QLatin1String("track_changes")
        || name == QLatin1String("functions") || name == QLatin1String("function") || name == QLatin1String("calculate")
        || name == QLatin1String("manage_search") || name == QLatin1String("format_list_numbered")
        || name == QLatin1String("format_list_bulleted") || name == QLatin1String("bookmark")
        || name == QLatin1String("rule")) {
        return QColor(0x7b, 0x1f, 0xa2); // data and formulas: purple
    }
    if (name == QLatin1String("spellcheck") || name == QLatin1String("lock") || name == QLatin1String("lock_open")
        || name == QLatin1String("bug_report")) {
        return QColor(0xd9, 0x30, 0x25); // review and protection: red
    }
    if (name == QLatin1String("bar_chart") || name == QLatin1String("image") || name == QLatin1String("category")
        || name == QLatin1String("insert_page_break") || name == QLatin1String("arrow_selector_tool")) {
        return QColor(0x1a, 0x73, 0xe8); // illustrations: blue
    }
    if (name == QLatin1String("draw") || name == QLatin1String("polyline") || name == QLatin1String("route")) {
        return QColor(0xe8, 0x71, 0x0a); // drawing tools: orange
    }
    if (name == QLatin1String("save") || name == QLatin1String("save_as") || name == QLatin1String("print")
        || name == QLatin1String("folder_open") || name == QLatin1String("note_add")
        || name == QLatin1String("ios_share") || name == QLatin1String("description")
        || name == QLatin1String("link") || name == QLatin1String("help") || name == QLatin1String("info")
        || name == QLatin1String("translate")) {
        return QColor(0x1a, 0x73, 0xe8); // files, links and help: blue
    }
    if (name == QLatin1String("comment") || name == QLatin1String("view_column")
        || name == QLatin1String("table_rows") || name == QLatin1String("tab")) {
        return QColor(0x00, 0x89, 0x7b); // comments and headings: teal
    }
    if (name == QLatin1String("search") || name == QLatin1String("find_replace")
        || name == QLatin1String("library_add") || name == QLatin1String("bookmark")) {
        return QColor(0x7b, 0x1f, 0xa2); // search and insertions: purple
    }
    return QColor(); // text formatting stays neutral, like the Office ribbon
}

// Render a shipped icon in its accent colour, or in the window text colour for
// commands whose family has no accent. The rendered icons are cached because
// the same symbol is reused by several actions and applications.
QIcon coloredIcon(const QString &name)
{
    QColor tint = iconTint(name);
    if (!tint.isValid()) {
        tint = qApp ? QApplication::palette().color(QPalette::WindowText) : QColor(Qt::black);
    }
    static QHash<QString, QIcon> cache;
    const QString key = name + QLatin1Char('#') + QString::number(tint.rgba(), 16);
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd()) {
        return cached.value();
    }
    const QIcon icon = bundledIcon(name);
    QIcon result = icon;
    if (!icon.isNull()) {
        QIcon colored;
        // Render the sizes the ribbon, tool panels and presenters ask for so
        // large buttons stay crisp instead of scaling a small pixmap.
        for (int size : {16, 20, 24, 32, 40, 48}) {
            QPixmap pixmap = icon.pixmap(size, size);
            if (pixmap.isNull()) {
                continue;
            }
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), tint);
            painter.end();
            colored.addPixmap(pixmap);
        }
        if (!colored.isNull()) {
            result = colored;
        }
    }
    cache.insert(key, result);
    return result;
}

const QHash<QString, QString> &themeSymbolTable()
{
    static const QHash<QString, QString> table = {
        // Editing and files
        {QStringLiteral("edit-copy"), QStringLiteral("content_copy")},
        {QStringLiteral("edit-cut"), QStringLiteral("content_cut")},
        {QStringLiteral("edit-paste"), QStringLiteral("content_paste")},
        {QStringLiteral("edit-delete"), QStringLiteral("delete")},
        {QStringLiteral("delete-symbolic"), QStringLiteral("delete")},
        {QStringLiteral("edit-clear"), QStringLiteral("delete_sweep")},
        {QStringLiteral("edit-find"), QStringLiteral("search")},
        {QStringLiteral("edit-find-replace"), QStringLiteral("find_replace")},
        {QStringLiteral("edit-undo"), QStringLiteral("restart_alt")},
        {QStringLiteral("edit-redo"), QStringLiteral("restart_alt")},
        {QStringLiteral("document-open"), QStringLiteral("folder_open")},
        {QStringLiteral("document-save"), QStringLiteral("save")},
        {QStringLiteral("document-save-as"), QStringLiteral("save_as")},
        {QStringLiteral("document-print"), QStringLiteral("print")},
        {QStringLiteral("document-print-preview"), QStringLiteral("print")},
        {QStringLiteral("document-properties"), QStringLiteral("description")},
        {QStringLiteral("document-edit"), QStringLiteral("description")},
        // Text formatting
        {QStringLiteral("format-text-bold"), QStringLiteral("format_bold")},
        {QStringLiteral("format-text-italic"), QStringLiteral("format_italic")},
        {QStringLiteral("format-text-underline"), QStringLiteral("format_underlined")},
        {QStringLiteral("format-text-strikethrough"), QStringLiteral("format_strikethrough")},
        {QStringLiteral("format-text-color"), QStringLiteral("format_color_text")},
        {QStringLiteral("format-fill-color"), QStringLiteral("format_color_fill")},
        {QStringLiteral("format-justify-left"), QStringLiteral("format_align_left")},
        {QStringLiteral("format-justify-center"), QStringLiteral("format_align_center")},
        {QStringLiteral("format-justify-right"), QStringLiteral("format_align_right")},
        {QStringLiteral("format-indent-more"), QStringLiteral("format_indent_increase")},
        {QStringLiteral("format-indent-less"), QStringLiteral("format_indent_decrease")},
        {QStringLiteral("format-line-spacing-normal"), QStringLiteral("format_line_spacing")},
        {QStringLiteral("format-line-spacing-double"), QStringLiteral("format_line_spacing")},
        {QStringLiteral("format-list-ordered"), QStringLiteral("format_list_numbered")},
        {QStringLiteral("format-list-unordered"), QStringLiteral("format_list_bulleted")},
        {QStringLiteral("format-text-hyphenation"), QStringLiteral("wrap_text")},
        // Insert
        {QStringLiteral("insert-image"), QStringLiteral("image")},
        {QStringLiteral("insert-table"), QStringLiteral("table_chart")},
        {QStringLiteral("insert-page-break"), QStringLiteral("insert_page_break")},
        {QStringLiteral("insert-footnote"), QStringLiteral("description")},
        {QStringLiteral("insert-endnote"), QStringLiteral("description")},
        {QStringLiteral("insert-table-of-contents"), QStringLiteral("format_list_bulleted")},
        {QStringLiteral("insert-text"), QStringLiteral("text_fields")},
        {QStringLiteral("insert-text-frame"), QStringLiteral("text_fields")},
        {QStringLiteral("character-set"), QStringLiteral("special_character")},
        // Drawing
        {QStringLiteral("draw-brush"), QStringLiteral("format_paint")},
        {QStringLiteral("draw-freehand"), QStringLiteral("draw")},
        {QStringLiteral("format-convert-to-path"), QStringLiteral("polyline")},
        {QStringLiteral("object-rotate-left"), QStringLiteral("restart_alt")},
        {QStringLiteral("object-rotate-right"), QStringLiteral("restart_alt")},
        // Review and language
        {QStringLiteral("tools-check-spelling"), QStringLiteral("spellcheck")},
        {QStringLiteral("tools-wizard"), QStringLiteral("settings")},
        {QStringLiteral("preferences-desktop-locale"), QStringLiteral("translate")},
        {QStringLiteral("preferences-other"), QStringLiteral("settings")},
        {QStringLiteral("configure"), QStringLiteral("settings")},
        // Navigation and view
        {QStringLiteral("go-previous"), QStringLiteral("chevron_left")},
        {QStringLiteral("go-next"), QStringLiteral("chevron_right")},
        {QStringLiteral("go-first"), QStringLiteral("first_page")},
        {QStringLiteral("go-last"), QStringLiteral("last_page")},
        {QStringLiteral("go-jump"), QStringLiteral("pin_drop")},
        {QStringLiteral("arrow-up"), QStringLiteral("arrow_upward")},
        {QStringLiteral("arrow-down"), QStringLiteral("arrow_downward")},
        {QStringLiteral("list-add"), QStringLiteral("library_add")},
        {QStringLiteral("list-remove"), QStringLiteral("delete")},
        {QStringLiteral("view-list-icons"), QStringLiteral("format_list_bulleted")},
        {QStringLiteral("view-list-details"), QStringLiteral("format_list_bulleted")},
        {QStringLiteral("view-grid"), QStringLiteral("border_all")},
        {QStringLiteral("view-refresh"), QStringLiteral("refresh")},
        {QStringLiteral("view-presentation"), QStringLiteral("fullscreen")},
        {QStringLiteral("show-ruler"), QStringLiteral("width_normal")},
        {QStringLiteral("zoom-in"), QStringLiteral("zoom_in")},
        {QStringLiteral("zoom-out"), QStringLiteral("zoom_out")},
        {QStringLiteral("zoom-fit-width"), QStringLiteral("width_normal")},
        {QStringLiteral("zoom-fit-best"), QStringLiteral("fit_screen")},
        {QStringLiteral("zoom-original"), QStringLiteral("fullscreen")},
        {QStringLiteral("zoom-select"), QStringLiteral("zoom_in_map")},
        {QStringLiteral("zoom-in-map"), QStringLiteral("zoom_in_map")},
        // Stage presentation and animation tools
        {QStringLiteral("media-playback-start"), QStringLiteral("fullscreen")},
        {QStringLiteral("edit_animation"), QStringLiteral("tune")},
        {QStringLiteral("system-run"), QStringLiteral("link")},
        {QStringLiteral("highlight-pointer-spot"), QStringLiteral("format_color_fill")},
        {QStringLiteral("pause-to-black-screen"), QStringLiteral("visibility_off")},
        {QStringLiteral("help-contents"), QStringLiteral("help")}
    };
    return table;
}

// Map ribbon action object names to the Material Symbols shipped in
// libs/main/icons/ribbon. Sheets, Words and Stage share the table, so a
// command keeps the same coloured icon wherever it appears. Actions keep
// their handlers, shortcuts and enabled/checked state.
struct CommandIcon {
    const char *action;
    const char *symbol;
};

const CommandIcon commandIcons[] = {
    // Sheets: Home
    {"paste", "content_paste"}, {"cut", "content_cut"}, {"copy", "content_copy"},
    {"specialPaste", "content_paste_go"}, {"increaseFontSize", "text_increase"},
    {"decreaseFontSize", "text_decrease"}, {"bold", "format_bold"}, {"italic", "format_italic"},
    {"underline", "format_underlined"}, {"strikeOut", "format_strikethrough"},
    {"backgroundColor", "format_color_fill"}, {"textColor", "format_color_text"},
    {"borderAll", "border_all"}, {"borderLeft", "border_left"}, {"borderTop", "border_top"},
    {"borderRight", "border_right"}, {"borderBottom", "border_bottom"},
    {"borderOutline", "border_outer"}, {"borderRemove", "border_clear"}, {"borderColor", "border_color"},
    {"alignTop", "vertical_align_top"}, {"alignMiddle", "vertical_align_center"},
    {"alignBottom", "vertical_align_bottom"}, {"alignLeft", "format_align_left"},
    {"alignCenter", "format_align_center"}, {"alignRight", "format_align_right"},
    {"changeAngle", "text_rotation_angleup"}, {"wrapText", "wrap_text"},
    {"verticalText", "text_rotate_vertical"}, {"decreaseIndentation", "format_indent_decrease"},
    {"increaseIndentation", "format_indent_increase"}, {"mergeCells", "cell_merge"},
    {"mergeCellsHorizontal", "combine_columns"}, {"mergeCellsVertical", "table_rows"},
    {"dissociateCells", "call_split"}, {"numeric", "numbers"}, {"currency", "attach_money"},
    {"percent", "percent"}, {"increasePrecision", "decimal_increase"},
    {"decreasePrecision", "decimal_decrease"}, {"conditional", "rule"},
    {"autoFormat", "table_chart"}, {"styleDialog", "style"}, {"insertCell", "add_box"},
    {"deleteCell", "delete"}, {"cellStyle", "format_paint"}, {"autoSum", "functions"},
    {"fillDown", "arrow_downward"}, {"fillRight", "arrow_forward"}, {"fillUp", "arrow_upward"},
    {"fillLeft", "arrow_back"}, {"sort", "sort"}, {"sortInc", "sort"}, {"sortDesc", "sort"},
    {"autoFilter", "filter_alt"}, {"customFilter", "filter_list"}, {"edit_find", "search"},
    {"edit_find_next", "search"}, {"edit_replace", "find_replace"}, {"selectAll", "select_all"},
    {"clearContents", "backspace"}, {"clearAll", "delete_sweep"}, {"clearComment", "comment"},
    {"clearConditional", "rule"}, {"clearHyperlink", "link_off"},
    // Sheets: File
    {"file_new", "note_add"}, {"file_open", "folder_open"}, {"file_save", "save"},
    {"file_save_as", "save_as"}, {"file_print", "print"}, {"createTemplate", "dashboard_customize"},
    {"file_encrypt_doc", "lock"}, {"file_uncompress_doc", "lock_open"},
    // Sheets: Insert
    {"insertColumn", "view_column"}, {"insertRow", "table_rows"}, {"insertSheet", "note_add"},
    {"add_shape", "category"}, {"insert_chart", "bar_chart"}, {"insert_picture", "image"},
    {"insertMathExpr", "functions"}, {"insertFormula", "function"},
    {"insertSeries", "format_list_numbered"}, {"insertSpecialChar", "special_character"},
    {"insert_textbox", "text_fields"}, {"insert_equation", "functions"},
    {"insertHyperlink", "link"}, {"comment", "comment"},
    {"insertFromClipboard", "content_paste"}, {"insertFromTextfile", "description"},
    {"insertFromDatabase", "database"},
    // Sheets: Page Layout
    {"setStyle", "style"}, {"createStyleFromCell", "library_add"}, {"adjust", "fit_screen"},
    {"resizeRow", "height"}, {"resizeCol", "width_normal"}, {"equalizeRow", "format_line_spacing"},
    {"equalizeCol", "format_line_spacing"}, {"hideRow", "visibility_off"}, {"showRow", "visibility"},
    {"hideColumn", "visibility_off"}, {"showColumn", "visibility"}, {"hideSheet", "visibility_off"},
    {"showSheet", "visibility"}, {"sheetProperties", "settings"}, {"paperLayout", "tune"},
    {"definePrintRange", "select_all"}, {"resetPrintRange", "restart_alt"},
    {"format_break_before_column", "insert_page_break"}, {"format_break_before_row", "insert_page_break"},
    {"shapeAnchor", "anchor"}, {"documentSettingsDialog", "settings"},
    // Sheets: Data
    {"textToColumns", "splitscreen"}, {"namedAreaDialog", "bookmark"}, {"consolidate", "merge"},
    {"subtotals", "functions"}, {"validity", "fact_check"}, {"goalSeek", "track_changes"},
    {"pivot", "pivot_table_chart"},
    // Sheets: Formulas and Review
    {"tools_spelling", "spellcheck"}, {"sortList", "format_list_bulleted"},
    {"protectSheet", "lock"}, {"protectDoc", "lock"}, {"RecalcWorkSheet", "refresh"},
    {"RecalcWorkBook", "refresh"}, {"showFormulas", "visibility"}, {"automaticCalculation", "calculate"},
    {"setAreaName", "library_add"},
    // Sheets: Navigate
    {"gotoCell", "pin_drop"}, {"go_first", "first_page"}, {"go_previous", "chevron_left"},
    {"go_next", "chevron_right"}, {"go_last", "last_page"},
    // Sheets: View
    {"view_newview", "open_in_new"}, {"showPageOutline", "border_outer"}, {"zoom_in", "zoom_in"},
    {"zoom_out", "zoom_out"}, {"view_zoom", "zoom_in_map"}, {"view_fullscreen", "fullscreen"},
    {"showColumnHeader", "view_column"}, {"showRowHeader", "table_rows"},
    {"showTabBar", "tab"}, {"showStatusBar", "info"},
    // Sheets: Help
    {"help_contents", "help"}, {"help_about_app", "info"}, {"help_report_bug", "bug_report"},
    {"help_about_kde", "info"}, {"switch_application_language", "translate"},

    // Words: Edit and formatting (the text tool's actions)
    {"edit_cut", "content_cut"}, {"edit_copy", "content_copy"}, {"edit_paste", "content_paste"},
    {"edit_paste_text", "content_paste_go"}, {"edit_select_all", "select_all"},
    {"edit_selectcurrentframe", "select_all"}, {"edit_delete", "delete"},
    {"format_painter", "format_paint"}, {"edit_findnext", "search"}, {"edit_findprevious", "search"},
    {"edit_comment", "comment"}, {"remove_comment", "comment"}, {"insert_annotation", "comment"},
    {"format_bold", "format_bold"}, {"format_italic", "format_italic"},
    {"format_underline", "format_underlined"}, {"format_strike", "format_strikethrough"},
    {"format_alignleft", "format_align_left"}, {"format_aligncenter", "format_align_center"},
    {"format_alignright", "format_align_right"},
    {"format_increaseindent", "format_indent_increase"},
    {"format_decreaseindent", "format_indent_decrease"}, {"format_list", "format_list_bulleted"},
    {"format_textcolor", "format_color_text"}, {"format_backgroundcolor", "format_color_fill"},
    {"border_backgroundcolor", "format_color_fill"},
    {"page_color", "format_color_fill"},
    {"format_stylist", "style"}, {"format_font", "text_fields"},
    {"format_paragraph", "format_line_spacing"}, {"format_page", "description"},
    {"watermark", "image"}, {"page_borders", "border_outer"},
    // Words: Insert
    {"insert_table", "table_chart"}, {"insert_variable", "bookmark"},
    {"insert_framebreak", "insert_page_break"}, {"insert_section", "splitscreen"},
    {"insert_header", "description"}, {"insert_footer", "description"}, {"insert_link", "link"},
    // Words: File
    {"extra_template", "dashboard_customize"}, {"file_print_preview", "print"},
    {"file_export_file", "ios_share"}, {"file_export_pdf", "ios_share"},
    {"file_documentinfo", "info"}, {"file_versions_file", "description"},
    // Words: View
    {"view_grid", "border_all"}, {"view_snaptogrid", "border_all"}, {"view_show_guides", "border_outer"},
    {"view_page_margins", "border_outer"}, {"view_frameborders", "border_outer"},
    {"view_tableborders", "border_all"}, {"view_sectionbounds", "border_outer"},
    {"show_ruler", "width_normal"}, {"view_wordCount", "numbers"},
    {"view_fieldshadings", "visibility"}, {"view_formattingchars", "text_fields"},
    {"view_notes", "description"}, {"show_docstruct", "format_list_bulleted"},
    {"view_toggledockers", "splitscreen"}, {"view_pagemode", "description"},
    {"view_previewmode", "visibility"}, {"view_textmode", "text_fields"},
    // Words: Review and language
    {"tool_auto_spellcheck", "spellcheck"}, {"tool_configure_spellcheck", "settings"},
    {"tools_autocorrect", "spellcheck"}, {"help_switch_application_language", "translate"},
    // Words: Word-specific ribbon commands
    {"ribbon_go_to_page", "pin_drop"},
    {"ribbon_horizontal_ruler", "width_normal"}, {"ribbon_vertical_ruler", "height"},
    {"ribbon_set_zoom", "zoom_in_map"}, {"ribbon_actual_size", "fullscreen"},
    {"ribbon_fit_width", "width_normal"}, {"ribbon_full_page", "fit_screen"},
    {"ribbon_fit_text", "fit_screen"}, {"ribbon_zoom_menu", "zoom_in"},
    {"ribbon_grid_guides", "border_all"}, {"ribbon_sidebars", "splitscreen"},
    {"ribbon_windows", "open_in_new"},
    {"convert-to-path", "polyline"}, {"rotate-left", "restart_alt"}, {"rotate-right", "restart_alt"},
    // Words: Mail merge
    {"mailmerge_select_recipients", "database"}, {"mailmerge_edit_recipients", "database"},
    {"mailmerge_insert_field", "library_add"}, {"mailmerge_greeting_line", "text_fields"},
    {"mailmerge_highlight", "visibility"}, {"mailmerge_preview", "visibility"},
    {"mailmerge_first", "first_page"}, {"mailmerge_prev", "chevron_left"},
    {"mailmerge_next", "chevron_right"}, {"mailmerge_last", "last_page"},
    {"mailmerge_finish", "fact_check"},
    // Words: tables
    {"insert_tablerow_above", "table_rows"}, {"insert_tablerow_below", "table_rows"},
    {"insert_tablecolumn_left", "view_column"}, {"insert_tablecolumn_right", "view_column"},
    {"delete_tablerow", "delete_sweep"}, {"delete_tablecolumn", "delete_sweep"},
    {"merge_tablecells", "cell_merge"}, {"split_tablecells", "call_split"},
    {"activate_borderpainter", "border_outer"},
    // Drawing tools (Words, Stage and Sheets)
    {"InteractionTool", "arrow_selector_tool"}, {"KoPencilTool", "draw"},
    {"CreatePathTool", "polyline"}, {"ConnectionTool", "route"},

    // Stage: Slides and Insert
    {"page_insertpage", "note_add"}, {"page_copypage", "content_copy"},
    {"page_deletepage", "delete"}, {"insert_pictures", "image"},
    {"stage_insert_textbox", "text_fields"}, {"import_document", "description"},
    // Objects offered by the Insert tab of Words and Stage
    {"insert_formula", "functions"}, {"insert_wordart", "style"},
    {"insert_stateshape", "fact_check"},
    {"insert_3dscene", "category"}, {"insert_vectorimage", "polyline"},
    {"insert_webshape", "link"},
    // Stage: Design and Slide Show
    {"format_masterpage", "dashboard_customize"}, {"format_pagelayout", "fit_screen"},
    {"slideshow_start_from_beginning", "fullscreen"},
    {"slideshow_start_from_current", "open_in_new"}, {"slideshow_configure", "settings"},
    {"edit_customslideshows", "format_list_numbered"},
    {"slideshow_presenterview", "splitscreen"},
    // Stage: View
    {"view_normal", "description"}, {"view_slides_sorter", "view_column"},
    {"view_masterpages", "dashboard_customize"}, {"view_rulers", "width_normal"}
};

const QHash<QString, QString> &commandIconTable()
{
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> icons;
        for (const CommandIcon &command : commandIcons) {
            icons.insert(QLatin1String(command.action), QLatin1String(command.symbol));
        }
        return icons;
    }();
    return table;
}

}

namespace KoRibbonIcons
{
QIcon forCommand(const QString &name)
{
    if (name.isEmpty()) {
        return QIcon();
    }
    const QHash<QString, QString> &commands = commandIconTable();
    const auto symbol = commands.constFind(name);
    if (symbol != commands.constEnd()) {
        return coloredIcon(symbol.value());
    }
    // Callers may also pass a symbol file name directly (menu families).
    return coloredIcon(name);
}

QIcon forThemeIcon(const QString &themeName, const QIcon &fallback)
{
    const auto symbol = themeSymbolTable().constFind(themeName);
    if (symbol != themeSymbolTable().constEnd()) {
        const QIcon icon = coloredIcon(symbol.value());
        if (!icon.isNull()) {
            return icon;
        }
    }
    return fallback.isNull() ? QIcon::fromTheme(themeName) : fallback;
}
}
