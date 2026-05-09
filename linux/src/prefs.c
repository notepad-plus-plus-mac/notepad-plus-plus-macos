#include "prefs.h"
#include "backup.h"
#include "encoding.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Global prefs instance — initialised to defaults                    */
/* ------------------------------------------------------------------ */

NppPrefs g_prefs = {
    .tab_width               = 4,
    .use_tabs                = FALSE,
    .auto_indent             = AUTO_INDENT_BASIC,
    .backspace_unindent      = FALSE,
    .highlight_current_line  = TRUE,
    .caret_width             = 1,
    .caret_blink_rate        = 600,
    .scroll_beyond_last_line = FALSE,
    .default_eol             = 2,  /* SC_EOL_LF */
    .default_encoding        = "UTF-8",
    .show_full_path_in_title = FALSE,
    .copy_line_no_selection  = TRUE,
    .autocomplete_enabled    = TRUE,
    .autocomplete_min_chars  = 1,
    .backup_enabled          = TRUE,
    .backup_interval_secs    = 60,
};

/* ------------------------------------------------------------------ */
/* XML load                                                            */
/* ------------------------------------------------------------------ */

static void xml_start(GMarkupParseContext *ctx, const gchar *el,
                      const gchar **names, const gchar **vals,
                      gpointer ud, GError **err)
{
    (void)ctx; (void)ud; (void)err;
    if (strcmp(el, "Pref") != 0) return;

    const char *name = NULL, *value = NULL;
    for (int i = 0; names[i]; i++) {
        if      (strcmp(names[i], "name")  == 0) name  = vals[i];
        else if (strcmp(names[i], "value") == 0) value = vals[i];
    }
    if (!name || !value) return;

    if      (strcmp(name, "tabWidth")              == 0) g_prefs.tab_width               = atoi(value);
    else if (strcmp(name, "useTabs")               == 0) g_prefs.use_tabs                = atoi(value) != 0;
    else if (strcmp(name, "autoIndent")            == 0) g_prefs.auto_indent             = atoi(value);
    else if (strcmp(name, "backspaceUnindent")     == 0) g_prefs.backspace_unindent      = atoi(value) != 0;
    else if (strcmp(name, "highlightCurrentLine")  == 0) g_prefs.highlight_current_line  = atoi(value) != 0;
    else if (strcmp(name, "caretWidth")            == 0) g_prefs.caret_width             = atoi(value);
    else if (strcmp(name, "caretBlinkRate")        == 0) g_prefs.caret_blink_rate        = atoi(value);
    else if (strcmp(name, "scrollBeyondLastLine")  == 0) g_prefs.scroll_beyond_last_line = atoi(value) != 0;
    else if (strcmp(name, "defaultEol")            == 0) g_prefs.default_eol             = atoi(value);
    else if (strcmp(name, "defaultEncoding")       == 0) { strncpy(g_prefs.default_encoding, value, 31); g_prefs.default_encoding[31] = '\0'; }
    else if (strcmp(name, "showFullPathInTitle")   == 0) g_prefs.show_full_path_in_title = atoi(value) != 0;
    else if (strcmp(name, "copyLineNoSelection")   == 0) g_prefs.copy_line_no_selection  = atoi(value) != 0;
    else if (strcmp(name, "autocompleteEnabled")   == 0) g_prefs.autocomplete_enabled    = atoi(value) != 0;
    else if (strcmp(name, "autocompleteMinChars")  == 0) g_prefs.autocomplete_min_chars  = atoi(value);
    else if (strcmp(name, "backupEnabled")         == 0) g_prefs.backup_enabled          = atoi(value) != 0;
    else if (strcmp(name, "backupIntervalSecs")    == 0) g_prefs.backup_interval_secs    = atoi(value);
}

static GMarkupParser s_parser = { xml_start, NULL, NULL, NULL, NULL };

void prefs_load(void)
{
    gchar *path = g_build_filename(g_get_home_dir(), ".config", "notetux", "config.xml", NULL);
    gchar *xml  = NULL;
    if (g_file_get_contents(path, &xml, NULL, NULL)) {
        GMarkupParseContext *ctx = g_markup_parse_context_new(&s_parser, 0, NULL, NULL);
        g_markup_parse_context_parse(ctx, xml, -1, NULL);
        g_markup_parse_context_free(ctx);
        g_free(xml);
    }
    g_free(path);

    /* Clamp values to valid ranges */
    if (g_prefs.tab_width      < 1)  g_prefs.tab_width      = 1;
    if (g_prefs.tab_width      > 16) g_prefs.tab_width      = 16;
    if (g_prefs.caret_width    < 1)  g_prefs.caret_width    = 1;
    if (g_prefs.caret_width    > 3)  g_prefs.caret_width    = 3;
    if (g_prefs.caret_blink_rate < 0)    g_prefs.caret_blink_rate = 0;
    if (g_prefs.caret_blink_rate > 2000) g_prefs.caret_blink_rate = 2000;
    if (g_prefs.default_eol < 0 || g_prefs.default_eol > 2) g_prefs.default_eol = 2;
    if (g_prefs.autocomplete_min_chars < 1)   g_prefs.autocomplete_min_chars = 1;
    if (g_prefs.autocomplete_min_chars > 10)  g_prefs.autocomplete_min_chars = 10;
    if (g_prefs.backup_interval_secs   < 10)  g_prefs.backup_interval_secs   = 10;
    if (g_prefs.backup_interval_secs   > 3600) g_prefs.backup_interval_secs  = 3600;
    if (g_prefs.default_encoding[0] == '\0')
        strncpy(g_prefs.default_encoding, "UTF-8", sizeof(g_prefs.default_encoding) - 1);
}

/* ------------------------------------------------------------------ */
/* XML save                                                            */
/* ------------------------------------------------------------------ */

void prefs_save(void)
{
    gchar *dir  = g_build_filename(g_get_home_dir(), ".config", "notetux", NULL);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    gchar *path = g_build_filename(g_get_home_dir(), ".config", "notetux", "config.xml", NULL);

    GString *buf = g_string_new(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<NotepadPlus>\n"
        "    <Preferences>\n");

#define WPREF_I(k, v) g_string_append_printf(buf, "        <Pref name=\"" k "\" value=\"%d\" />\n", (int)(v))
#define WPREF_S(k, v) g_string_append_printf(buf, "        <Pref name=\"" k "\" value=\"%s\" />\n", (v))

    WPREF_I("tabWidth",              g_prefs.tab_width);
    WPREF_I("useTabs",               g_prefs.use_tabs);
    WPREF_I("autoIndent",            g_prefs.auto_indent);
    WPREF_I("backspaceUnindent",     g_prefs.backspace_unindent);
    WPREF_I("highlightCurrentLine",  g_prefs.highlight_current_line);
    WPREF_I("caretWidth",            g_prefs.caret_width);
    WPREF_I("caretBlinkRate",        g_prefs.caret_blink_rate);
    WPREF_I("scrollBeyondLastLine",  g_prefs.scroll_beyond_last_line);
    WPREF_I("defaultEol",            g_prefs.default_eol);
    WPREF_S("defaultEncoding",       g_prefs.default_encoding);
    WPREF_I("showFullPathInTitle",   g_prefs.show_full_path_in_title);
    WPREF_I("copyLineNoSelection",   g_prefs.copy_line_no_selection);
    WPREF_I("autocompleteEnabled",   g_prefs.autocomplete_enabled);
    WPREF_I("autocompleteMinChars",  g_prefs.autocomplete_min_chars);
    WPREF_I("backupEnabled",         g_prefs.backup_enabled);
    WPREF_I("backupIntervalSecs",    g_prefs.backup_interval_secs);

#undef WPREF_I
#undef WPREF_S

    g_string_append(buf, "    </Preferences>\n</NotepadPlus>\n");
    g_file_set_contents(path, buf->str, (gssize)buf->len, NULL);
    g_string_free(buf, TRUE);
    g_free(path);
}

/* ------------------------------------------------------------------ */
/* Dialog — forward declarations                                       */
/* ------------------------------------------------------------------ */

/* Implemented in editor.c, applies g_prefs to all open editors */
void editor_apply_prefs(void);

/* Implemented in main.c, refreshes all window titles */
void main_refresh_title(void);

/* ------------------------------------------------------------------ */
/* Helper: labelled widget row                                         */
/* ------------------------------------------------------------------ */

static GtkWidget *row(GtkWidget *grid, int r, const char *label, GtkWidget *widget)
{
    GtkWidget *lbl = gtk_label_new(label);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_end(lbl, 12);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, r, 1, 1);
    gtk_widget_set_halign(widget, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, r, 1, 1);
    return widget;
}

static GtkWidget *make_grid(void)
{
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 8);
    gtk_grid_set_column_spacing(GTK_GRID(g), 12);
    gtk_widget_set_margin_start(g, 12);
    gtk_widget_set_margin_end(g, 12);
    gtk_widget_set_margin_top(g, 12);
    gtk_widget_set_margin_bottom(g, 12);
    return g;
}

/* ------------------------------------------------------------------ */
/* Signal callbacks — live apply on change                            */
/* ------------------------------------------------------------------ */

static void on_tab_width(GtkSpinButton *s, gpointer d)      { (void)d; g_prefs.tab_width = (int)gtk_spin_button_get_value(s); editor_apply_prefs(); prefs_save(); }
static void on_use_tabs(GtkToggleButton *b, gpointer d)     { (void)d; g_prefs.use_tabs = gtk_toggle_button_get_active(b); editor_apply_prefs(); prefs_save(); }
static void on_use_spaces(GtkToggleButton *b, gpointer d)   { (void)d; if (gtk_toggle_button_get_active(b)) { g_prefs.use_tabs = FALSE; editor_apply_prefs(); prefs_save(); } }
static void on_ai_none(GtkToggleButton *b, gpointer d)      { (void)d; if (gtk_toggle_button_get_active(b)) { g_prefs.auto_indent = AUTO_INDENT_NONE;     prefs_save(); } }
static void on_ai_basic(GtkToggleButton *b, gpointer d)     { (void)d; if (gtk_toggle_button_get_active(b)) { g_prefs.auto_indent = AUTO_INDENT_BASIC;    prefs_save(); } }
static void on_ai_adv(GtkToggleButton *b, gpointer d)       { (void)d; if (gtk_toggle_button_get_active(b)) { g_prefs.auto_indent = AUTO_INDENT_ADVANCED; prefs_save(); } }
static void on_bs_unindent(GtkToggleButton *b, gpointer d)  { (void)d; g_prefs.backspace_unindent = gtk_toggle_button_get_active(b); prefs_save(); }
static void on_ac_enable(GtkToggleButton *b, gpointer d)    { (void)d; g_prefs.autocomplete_enabled = gtk_toggle_button_get_active(b); prefs_save(); }
static void on_ac_min(GtkSpinButton *s, gpointer d)         { (void)d; g_prefs.autocomplete_min_chars = (int)gtk_spin_button_get_value(s); prefs_save(); }

static void on_hl_line(GtkToggleButton *b, gpointer d)      { (void)d; g_prefs.highlight_current_line = gtk_toggle_button_get_active(b); editor_apply_prefs(); prefs_save(); }
static void on_caret_w(GtkComboBox *c, gpointer d)          { (void)d; g_prefs.caret_width = gtk_combo_box_get_active(c) + 1; editor_apply_prefs(); prefs_save(); }
static void on_blink(GtkSpinButton *s, gpointer d)          { (void)d; g_prefs.caret_blink_rate = (int)gtk_spin_button_get_value(s); editor_apply_prefs(); prefs_save(); }
static void on_scroll_past(GtkToggleButton *b, gpointer d)  { (void)d; g_prefs.scroll_beyond_last_line = gtk_toggle_button_get_active(b); editor_apply_prefs(); prefs_save(); }

static void on_eol_lf(GtkToggleButton *b, gpointer d)       { (void)d; if (gtk_toggle_button_get_active(b)) { g_prefs.default_eol = 2; prefs_save(); } }
static void on_eol_crlf(GtkToggleButton *b, gpointer d)     { (void)d; if (gtk_toggle_button_get_active(b)) { g_prefs.default_eol = 0; prefs_save(); } }
static void on_eol_cr(GtkToggleButton *b, gpointer d)       { (void)d; if (gtk_toggle_button_get_active(b)) { g_prefs.default_eol = 1; prefs_save(); } }
static void on_enc_combo(GtkComboBox *c, gpointer d)
{
    (void)d;
    int idx = gtk_combo_box_get_active(c);
    if (idx >= 0 && idx < npp_encoding_count) {
        strncpy(g_prefs.default_encoding, npp_encodings[idx].display,
                sizeof(g_prefs.default_encoding) - 1);
        g_prefs.default_encoding[sizeof(g_prefs.default_encoding) - 1] = '\0';
    }
    prefs_save();
}

static void on_full_path(GtkToggleButton *b, gpointer d)    { (void)d; g_prefs.show_full_path_in_title = gtk_toggle_button_get_active(b); main_refresh_title(); prefs_save(); }
static void on_copy_line(GtkToggleButton *b, gpointer d)    { (void)d; g_prefs.copy_line_no_selection = gtk_toggle_button_get_active(b); prefs_save(); }

static GtkWidget *s_backup_interval_spin = NULL;

static void on_backup_enabled(GtkToggleButton *b, gpointer d)
{
    (void)d;
    g_prefs.backup_enabled = gtk_toggle_button_get_active(b);
    if (s_backup_interval_spin)
        gtk_widget_set_sensitive(s_backup_interval_spin, g_prefs.backup_enabled);
    backup_restart_timer();
    prefs_save();
}

static void on_backup_interval(GtkSpinButton *s, gpointer d)
{
    (void)d;
    g_prefs.backup_interval_secs = (int)gtk_spin_button_get_value(s);
    backup_restart_timer();
    prefs_save();
}

/* ------------------------------------------------------------------ */
/* Page builders                                                       */
/* ------------------------------------------------------------------ */

static GtkWidget *page_editor(void)
{
    GtkWidget *g = make_grid();

    /* Tab width */
    GtkWidget *spin_tw = gtk_spin_button_new_with_range(1, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_tw), g_prefs.tab_width);
    row(g, 0, "Tab / indent size:", spin_tw);
    g_signal_connect(spin_tw, "value-changed", G_CALLBACK(on_tab_width), NULL);

    /* Use tabs vs spaces */
    GtkWidget *radio_tabs   = gtk_radio_button_new_with_label(NULL, "Tab character");
    GtkWidget *radio_spaces = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_tabs), "Space characters");
    if (g_prefs.use_tabs) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_tabs),   TRUE);
    else                  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_spaces), TRUE);
    GtkWidget *indent_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(indent_box), radio_tabs);
    gtk_container_add(GTK_CONTAINER(indent_box), radio_spaces);
    row(g, 1, "Indent using:", indent_box);
    g_signal_connect(radio_tabs,   "toggled", G_CALLBACK(on_use_tabs),   NULL);
    g_signal_connect(radio_spaces, "toggled", G_CALLBACK(on_use_spaces), NULL);

    /* Auto-indent */
    GtkWidget *ai_none  = gtk_radio_button_new_with_label(NULL, "None");
    GtkWidget *ai_basic = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(ai_none), "Basic");
    GtkWidget *ai_adv   = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(ai_none), "Advanced (detect { :)");
    if (g_prefs.auto_indent == AUTO_INDENT_NONE)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ai_none),  TRUE);
    else if (g_prefs.auto_indent == AUTO_INDENT_ADVANCED)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ai_adv),   TRUE);
    else
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ai_basic), TRUE);
    GtkWidget *ai_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(ai_box), ai_none);
    gtk_container_add(GTK_CONTAINER(ai_box), ai_basic);
    gtk_container_add(GTK_CONTAINER(ai_box), ai_adv);
    row(g, 2, "Auto-indent:", ai_box);
    g_signal_connect(ai_none,  "toggled", G_CALLBACK(on_ai_none),  NULL);
    g_signal_connect(ai_basic, "toggled", G_CALLBACK(on_ai_basic), NULL);
    g_signal_connect(ai_adv,   "toggled", G_CALLBACK(on_ai_adv),   NULL);

    /* Backspace unindent */
    GtkWidget *chk_bs = gtk_check_button_new_with_label("Backspace key unindents instead of removing single space");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_bs), g_prefs.backspace_unindent);
    gtk_grid_attach(GTK_GRID(g), chk_bs, 0, 3, 2, 1);
    g_signal_connect(chk_bs, "toggled", G_CALLBACK(on_bs_unindent), NULL);

    /* Auto-completion */
    GtkWidget *chk_ac = gtk_check_button_new_with_label("Enable auto-completion (keywords + document words)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_ac), g_prefs.autocomplete_enabled);
    gtk_grid_attach(GTK_GRID(g), chk_ac, 0, 4, 2, 1);
    g_signal_connect(chk_ac, "toggled", G_CALLBACK(on_ac_enable), NULL);

    GtkWidget *spin_ac = gtk_spin_button_new_with_range(1, 10, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_ac), g_prefs.autocomplete_min_chars);
    row(g, 5, "Auto-complete from N characters:", spin_ac);
    g_signal_connect(spin_ac, "value-changed", G_CALLBACK(on_ac_min), NULL);

    return g;
}

static GtkWidget *page_display(void)
{
    GtkWidget *g = make_grid();

    /* Highlight current line */
    GtkWidget *chk_hl = gtk_check_button_new_with_label("Highlight current line");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_hl), g_prefs.highlight_current_line);
    gtk_grid_attach(GTK_GRID(g), chk_hl, 0, 0, 2, 1);
    g_signal_connect(chk_hl, "toggled", G_CALLBACK(on_hl_line), NULL);

    /* Caret width */
    GtkWidget *caret_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(caret_combo), "Thin (1 px)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(caret_combo), "Medium (2 px)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(caret_combo), "Thick (3 px)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(caret_combo), g_prefs.caret_width - 1);
    row(g, 1, "Caret width:", caret_combo);
    g_signal_connect(caret_combo, "changed", G_CALLBACK(on_caret_w), NULL);

    /* Caret blink rate */
    GtkWidget *spin_blink = gtk_spin_button_new_with_range(0, 2000, 50);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_blink), g_prefs.caret_blink_rate);
    row(g, 2, "Caret blink rate (ms, 0 = no blink):", spin_blink);
    g_signal_connect(spin_blink, "value-changed", G_CALLBACK(on_blink), NULL);

    /* Scroll beyond last line */
    GtkWidget *chk_scroll = gtk_check_button_new_with_label("Scroll past end of file");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_scroll), g_prefs.scroll_beyond_last_line);
    gtk_grid_attach(GTK_GRID(g), chk_scroll, 0, 3, 2, 1);
    g_signal_connect(chk_scroll, "toggled", G_CALLBACK(on_scroll_past), NULL);

    return g;
}

static GtkWidget *page_new_document(void)
{
    GtkWidget *g = make_grid();

    /* Default EOL */
    GtkWidget *eol_lf   = gtk_radio_button_new_with_label(NULL, "Unix (LF)");
    GtkWidget *eol_crlf = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(eol_lf),   "Windows (CRLF)");
    GtkWidget *eol_cr   = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(eol_lf),   "Old Mac (CR)");
    /* SC_EOL_CRLF=0, SC_EOL_CR=1, SC_EOL_LF=2 */
    switch (g_prefs.default_eol) {
        case 0:  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(eol_crlf), TRUE); break;
        case 1:  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(eol_cr),   TRUE); break;
        default: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(eol_lf),   TRUE); break;
    }
    GtkWidget *eol_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(eol_box), eol_lf);
    gtk_container_add(GTK_CONTAINER(eol_box), eol_crlf);
    gtk_container_add(GTK_CONTAINER(eol_box), eol_cr);
    row(g, 0, "Default line ending:", eol_box);
    g_signal_connect(eol_lf,   "toggled", G_CALLBACK(on_eol_lf),   NULL);
    g_signal_connect(eol_crlf, "toggled", G_CALLBACK(on_eol_crlf), NULL);
    g_signal_connect(eol_cr,   "toggled", G_CALLBACK(on_eol_cr),   NULL);

    /* Default encoding */
    GtkWidget *enc_combo = gtk_combo_box_text_new();
    int active_enc = 0;
    for (int i = 0; i < npp_encoding_count; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(enc_combo), npp_encodings[i].display);
        if (strcmp(npp_encodings[i].display, g_prefs.default_encoding) == 0)
            active_enc = i;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(enc_combo), active_enc);
    row(g, 1, "Default encoding:", enc_combo);
    g_signal_connect(enc_combo, "changed", G_CALLBACK(on_enc_combo), NULL);

    return g;
}

static GtkWidget *page_general(void)
{
    GtkWidget *g = make_grid();

    GtkWidget *chk_fp = gtk_check_button_new_with_label("Show full file path in title bar");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_fp), g_prefs.show_full_path_in_title);
    gtk_grid_attach(GTK_GRID(g), chk_fp, 0, 0, 2, 1);
    g_signal_connect(chk_fp, "toggled", G_CALLBACK(on_full_path), NULL);

    GtkWidget *chk_cl = gtk_check_button_new_with_label("Copy / cut whole line when nothing is selected");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_cl), g_prefs.copy_line_no_selection);
    gtk_grid_attach(GTK_GRID(g), chk_cl, 0, 1, 2, 1);
    g_signal_connect(chk_cl, "toggled", G_CALLBACK(on_copy_line), NULL);

    return g;
}

static GtkWidget *page_backup(void)
{
    GtkWidget *g = make_grid();

    GtkWidget *chk = gtk_check_button_new_with_label("Enable auto-backup for unsaved changes");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), g_prefs.backup_enabled);
    gtk_grid_attach(GTK_GRID(g), chk, 0, 0, 2, 1);
    g_signal_connect(chk, "toggled", G_CALLBACK(on_backup_enabled), NULL);

    s_backup_interval_spin = gtk_spin_button_new_with_range(10, 3600, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s_backup_interval_spin),
                              g_prefs.backup_interval_secs);
    gtk_widget_set_sensitive(s_backup_interval_spin, g_prefs.backup_enabled);
    row(g, 1, "Backup interval (seconds):", s_backup_interval_spin);
    g_signal_connect(s_backup_interval_spin, "value-changed",
                     G_CALLBACK(on_backup_interval), NULL);

    GtkWidget *info = gtk_label_new("Backup files are written to ~/.config/notetux/backup/\n"
                                    "and removed when the file is saved or closed.");
    gtk_widget_set_halign(info, GTK_ALIGN_START);
    gtk_widget_set_margin_top(info, 8);
    gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
    gtk_grid_attach(GTK_GRID(g), info, 0, 2, 2, 1);

    return g;
}

/* ------------------------------------------------------------------ */
/* Dialog                                                              */
/* ------------------------------------------------------------------ */

static GtkWidget *s_prefs_dlg = NULL;

static void on_prefs_response(GtkDialog *dlg, gint r, gpointer d)
{
    (void)r; (void)d;
    gtk_widget_hide(GTK_WIDGET(dlg));
}

void prefs_dialog_show(GtkWidget *parent)
{
    if (s_prefs_dlg) {
        gtk_window_set_transient_for(GTK_WINDOW(s_prefs_dlg), GTK_WINDOW(parent));
        gtk_window_present(GTK_WINDOW(s_prefs_dlg));
        return;
    }

    s_prefs_dlg = gtk_dialog_new_with_buttons(
        "Preferences",
        GTK_WINDOW(parent),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_resizable(GTK_WINDOW(s_prefs_dlg), FALSE);

    GtkWidget *nb = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), page_editor(),       gtk_label_new("Editor"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), page_display(),      gtk_label_new("Display"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), page_new_document(), gtk_label_new("New Document"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), page_general(),      gtk_label_new("General"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), page_backup(),       gtk_label_new("Backup"));

    GtkWidget *ca = gtk_dialog_get_content_area(GTK_DIALOG(s_prefs_dlg));
    gtk_container_set_border_width(GTK_CONTAINER(ca), 8);
    gtk_container_add(GTK_CONTAINER(ca), nb);

    g_signal_connect(s_prefs_dlg, "response", G_CALLBACK(on_prefs_response), NULL);
    gtk_widget_show_all(s_prefs_dlg);
}
