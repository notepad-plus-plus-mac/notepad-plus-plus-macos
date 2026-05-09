/* styleeditor.c — Style Configurator dialog for the Linux GTK3 port.
 * Ports StyleConfiguratorWindowController from the macOS version.
 *
 * Layout:
 *   [Theme dropdown]
 *   ┌─────────────────┬──────────────────────────────────────────────┐
 *   │ Language list   │  Style list                                  │
 *   │ (GtkListBox)    │  (GtkListBox)                                │
 *   │                 ├──────────────────────────────────────────────┤
 *   │                 │  Attribute panel                             │
 *   │                 │   Font: [GtkFontButton]                      │
 *   │                 │   Fg:   [☐ enabled] [color]                  │
 *   │                 │   Bg:   [☐ enabled] [color]                  │
 *   │                 │   [Bold] [Italic] [Underline]                │
 *   │                 │   Preview: ████████████                      │
 *   └─────────────────┴──────────────────────────────────────────────┘
 *   [Apply to Editors]  [Save]  [Close]
 */
#include "styleeditor.h"
#include "stylestore.h"
#include "i18n.h"
#include <string.h>
#include <stdio.h>

#ifndef RESOURCES_DIR
#define RESOURCES_DIR "../../resources"
#endif

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* BGR (Scintilla) ↔ GdkRGBA */
static GdkRGBA bgr_to_rgba(int bgr)
{
    GdkRGBA c;
    c.red   = (bgr & 0xFF)         / 255.0;
    c.green = ((bgr >>  8) & 0xFF) / 255.0;
    c.blue  = ((bgr >> 16) & 0xFF) / 255.0;
    c.alpha = 1.0;
    return c;
}

static int rgba_to_bgr(const GdkRGBA *c)
{
    int r = (int)(c->red   * 255 + 0.5);
    int g = (int)(c->green * 255 + 0.5);
    int b = (int)(c->blue  * 255 + 0.5);
    return r | (g << 8) | (b << 16);
}

/* Scan a directory for *.xml files; returns a newly-allocated GPtrArray
 * of g_strdup'd paths.  Caller frees with g_ptr_array_unref(). */
static GPtrArray *scan_themes(const char *dir)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    GError *err = NULL;
    GDir *d = g_dir_open(dir, 0, &err);
    if (!d) { if (err) g_error_free(err); return arr; }
    const char *name;
    while ((name = g_dir_read_name(d))) {
        if (!g_str_has_suffix(name, ".xml")) continue;
        g_ptr_array_add(arr, g_build_filename(dir, name, NULL));
    }
    g_dir_close(d);
    /* sort by basename */
    g_ptr_array_sort(arr, (GCompareFunc)g_strcmp0);
    return arr;
}

/* ------------------------------------------------------------------ */
/* Dialog state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    /* widgets */
    GtkWidget *dialog;
    GtkWidget *theme_combo;
    GtkWidget *lang_list;        /* GtkListBox */
    GtkWidget *style_list;       /* GtkListBox */
    GtkWidget *font_btn;         /* GtkFontButton */
    GtkWidget *fg_check;
    GtkWidget *fg_btn;           /* GtkColorButton */
    GtkWidget *bg_check;
    GtkWidget *bg_btn;
    GtkWidget *bold_check;
    GtkWidget *italic_check;
    GtkWidget *underline_check;
    GtkWidget *preview_label;

    /* selection state */
    int sel_block;   /* index into stylestore blocks, -1 = none */
    int sel_entry;   /* index into selected block's entries, -1 = none */

    gboolean loading;   /* suppress change callbacks during load */
    gboolean changed;   /* any edits since last save */

    GPtrArray *theme_paths;  /* absolute paths for combo box entries */
    SEApplyFn  on_apply;     /* caller callback: re-apply styles to editors */
} SEState;

/* ------------------------------------------------------------------ */
/* Update preview label                                               */
/* ------------------------------------------------------------------ */

static void update_preview(SEState *s)
{
    if (s->sel_block < 0 || s->sel_entry < 0) return;

    NppStyleEntry e;
    if (!stylestore_get_entry(s->sel_block, s->sel_entry, &e)) return;

    /* Build Pango markup */
    char fg_hex[8] = "#000000";
    char bg_hex[8] = "#FFFFFF";
    if (e.fg >= 0) {
        int r = e.fg & 0xFF, g = (e.fg>>8)&0xFF, b = (e.fg>>16)&0xFF;
        snprintf(fg_hex, sizeof(fg_hex), "#%02X%02X%02X", r, g, b);
    }
    if (e.bg >= 0) {
        int r = e.bg & 0xFF, g = (e.bg>>8)&0xFF, b = (e.bg>>16)&0xFF;
        snprintf(bg_hex, sizeof(bg_hex), "#%02X%02X%02X", r, g, b);
    }

    char markup[512];
    int off = snprintf(markup, sizeof(markup),
        "<span foreground=\"%s\" background=\"%s\"", fg_hex, bg_hex);
    if (e.font_name[0])
        off += snprintf(markup+off, sizeof(markup)-off,
            " font_family=\"%s\"", e.font_name);
    if (e.font_size > 0)
        off += snprintf(markup+off, sizeof(markup)-off,
            " size=\"%d\"", e.font_size * 1024);
    if (e.bold > 0)
        off += snprintf(markup+off, sizeof(markup)-off, " weight=\"bold\"");
    if (e.italic > 0)
        off += snprintf(markup+off, sizeof(markup)-off, " style=\"italic\"");
    if (e.underline > 0)
        off += snprintf(markup+off, sizeof(markup)-off, " underline=\"single\"");
    snprintf(markup+off, sizeof(markup)-off, ">  AaBbCcDd 123  </span>");

    gtk_label_set_markup(GTK_LABEL(s->preview_label), markup);
}

/* ------------------------------------------------------------------ */
/* Load entry into attribute panel                                    */
/* ------------------------------------------------------------------ */

static void load_entry_to_panel(SEState *s)
{
    if (s->sel_block < 0 || s->sel_entry < 0) return;

    NppStyleEntry e;
    if (!stylestore_get_entry(s->sel_block, s->sel_entry, &e)) return;

    s->loading = TRUE;

    /* Font button — build a Pango font description string */
    char fdesc[128];
    const char *fn = e.font_name[0] ? e.font_name : "Monospace";
    int  fs = e.font_size > 0 ? e.font_size : 10;
    snprintf(fdesc, sizeof(fdesc), "%s %d", fn, fs);
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(s->font_btn), fdesc);

    /* Foreground */
    gboolean has_fg = (e.fg >= 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->fg_check), has_fg);
    gtk_widget_set_sensitive(s->fg_btn, has_fg);
    if (has_fg) {
        GdkRGBA c = bgr_to_rgba(e.fg);
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->fg_btn), &c);
    }

    /* Background */
    gboolean has_bg = (e.bg >= 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->bg_check), has_bg);
    gtk_widget_set_sensitive(s->bg_btn, has_bg);
    if (has_bg) {
        GdkRGBA c = bgr_to_rgba(e.bg);
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->bg_btn), &c);
    }

    /* Bold / italic / underline */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->bold_check),
                                  e.bold > 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->italic_check),
                                  e.italic > 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->underline_check),
                                  e.underline > 0);

    s->loading = FALSE;
    update_preview(s);
}

/* ------------------------------------------------------------------ */
/* Save attribute panel back to store                                 */
/* ------------------------------------------------------------------ */

static void save_panel_to_store(SEState *s)
{
    if (s->loading) return;
    if (s->sel_block < 0 || s->sel_entry < 0) return;

    NppStyleEntry e;
    if (!stylestore_get_entry(s->sel_block, s->sel_entry, &e)) return;

    /* Font */
    const char *fdesc = gtk_font_chooser_get_font(
        GTK_FONT_CHOOSER(s->font_btn));
    if (fdesc && *fdesc) {
        /* Separate "FontName SIZE" */
        const char *last = strrchr(fdesc, ' ');
        if (last && *(last+1)) {
            int sz = atoi(last + 1);
            if (sz > 0) e.font_size = sz;
            int len = (int)(last - fdesc);
            if (len > 0 && len < (int)sizeof(e.font_name)) {
                memcpy(e.font_name, fdesc, (size_t)len);
                e.font_name[len] = '\0';
            }
        } else {
            g_strlcpy(e.font_name, fdesc, sizeof(e.font_name));
        }
    }

    /* Foreground */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->fg_check))) {
        GdkRGBA c;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->fg_btn), &c);
        e.fg = rgba_to_bgr(&c);
    } else {
        e.fg = -1;
    }

    /* Background */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->bg_check))) {
        GdkRGBA c;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->bg_btn), &c);
        e.bg = rgba_to_bgr(&c);
    } else {
        e.bg = -1;
    }

    e.bold      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->bold_check))      ? 1 : 0;
    e.italic    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->italic_check))    ? 1 : 0;
    e.underline = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->underline_check)) ? 1 : 0;

    stylestore_set_entry(s->sel_block, s->sel_entry, &e);
    s->changed = TRUE;
    update_preview(s);
}

/* ------------------------------------------------------------------ */
/* Populate style list for selected language block                    */
/* ------------------------------------------------------------------ */

static void populate_style_list(SEState *s)
{
    /* remove existing rows */
    GList *children = gtk_container_get_children(GTK_CONTAINER(s->style_list));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    s->sel_entry = -1;

    if (s->sel_block < 0) return;

    int count = stylestore_entry_count(s->sel_block);
    for (int j = 0; j < count; j++) {
        NppStyleEntry e;
        if (!stylestore_get_entry(s->sel_block, j, &e)) continue;
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new(e.name[0] ? e.name : "(unnamed)");
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_margin_start(lbl, 6);
        gtk_widget_set_margin_end(lbl, 6);
        gtk_widget_set_margin_top(lbl, 2);
        gtk_widget_set_margin_bottom(lbl, 2);
        gtk_container_add(GTK_CONTAINER(row), lbl);
        g_object_set_data(G_OBJECT(row), "entry-idx", GINT_TO_POINTER(j));
        gtk_container_add(GTK_CONTAINER(s->style_list), row);
    }
    gtk_widget_show_all(s->style_list);
}

/* ------------------------------------------------------------------ */
/* Populate language list                                             */
/* ------------------------------------------------------------------ */

static void populate_lang_list(SEState *s)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(s->lang_list));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    s->sel_block = -1;
    s->sel_entry = -1;

    int bc = stylestore_block_count();
    for (int i = 0; i < bc; i++) {
        const char *id = stylestore_block_id(i);
        if (!id) continue;
        const char *label = (strcmp(id, "global") == 0) ? "Global Styles" : id;
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new(label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_margin_start(lbl, 6);
        gtk_widget_set_margin_end(lbl, 6);
        gtk_widget_set_margin_top(lbl, 2);
        gtk_widget_set_margin_bottom(lbl, 2);
        gtk_container_add(GTK_CONTAINER(row), lbl);
        g_object_set_data(G_OBJECT(row), "block-idx", GINT_TO_POINTER(i));
        gtk_container_add(GTK_CONTAINER(s->lang_list), row);
    }
    gtk_widget_show_all(s->lang_list);
}

/* ------------------------------------------------------------------ */
/* Theme combo                                                        */
/* ------------------------------------------------------------------ */

static void populate_theme_combo(SEState *s)
{
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(s->theme_combo));
    g_ptr_array_set_size(s->theme_paths, 0);

    /* Entry 0 = default model */
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->theme_combo),
                                   "Default");
    g_ptr_array_add(s->theme_paths, g_strdup(""));  /* empty = default */

    /* Bundled themes */
    char bdir[512];
    snprintf(bdir, sizeof(bdir), RESOURCES_DIR "/themes");
    GPtrArray *bundled = scan_themes(bdir);
    for (guint i = 0; i < bundled->len; i++) {
        const char *p = (const char *)g_ptr_array_index(bundled, i);
        char *base = g_path_get_basename(p);
        /* strip .xml */
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->theme_combo), base);
        g_ptr_array_add(s->theme_paths, g_strdup(p));
        g_free(base);
    }
    g_ptr_array_unref(bundled);

    /* User themes from $HOME/.config/notetux/themes/ */
    const char *home = g_get_home_dir();
    if (home) {
        char udir[512];
        snprintf(udir, sizeof(udir), "%s/.config/notetux/themes", home);
        GPtrArray *user = scan_themes(udir);
        for (guint i = 0; i < user->len; i++) {
            const char *p = (const char *)g_ptr_array_index(user, i);
            char *base = g_path_get_basename(p);
            char *dot = strrchr(base, '.');
            if (dot) *dot = '\0';
            char label[128];
            snprintf(label, sizeof(label), "%s (user)", base);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->theme_combo),
                                           label);
            g_ptr_array_add(s->theme_paths, g_strdup(p));
            g_free(base);
        }
        g_ptr_array_unref(user);
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(s->theme_combo), 0);
}

/* ------------------------------------------------------------------ */
/* Signal callbacks                                                   */
/* ------------------------------------------------------------------ */

static void on_theme_changed(GtkComboBox *combo, gpointer data)
{
    SEState *s = (SEState *)data;
    int idx = gtk_combo_box_get_active(combo);
    if (idx < 0 || idx >= (int)s->theme_paths->len) return;

    const char *path = (const char *)g_ptr_array_index(s->theme_paths,
                                                        (guint)idx);
    stylestore_load_theme(path && *path ? path : NULL);
    populate_lang_list(s);
    populate_style_list(s);
    s->changed = TRUE;
}

static void on_lang_row_selected(GtkListBox *lb, GtkListBoxRow *row, gpointer data)
{
    (void)lb;
    SEState *s = (SEState *)data;
    if (!row) { s->sel_block = -1; populate_style_list(s); return; }
    s->sel_block = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "block-idx"));
    populate_style_list(s);
}

static void on_style_row_selected(GtkListBox *lb, GtkListBoxRow *row, gpointer data)
{
    (void)lb;
    SEState *s = (SEState *)data;
    if (!row) { s->sel_entry = -1; return; }
    s->sel_entry = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "entry-idx"));
    load_entry_to_panel(s);
}

static void on_font_set(GtkFontButton *fb, gpointer data)
{
    (void)fb;
    save_panel_to_store((SEState *)data);
}

static void on_fg_check_toggled(GtkToggleButton *tb, gpointer data)
{
    SEState *s = (SEState *)data;
    gboolean on = gtk_toggle_button_get_active(tb);
    gtk_widget_set_sensitive(s->fg_btn, on);
    save_panel_to_store(s);
}

static void on_bg_check_toggled(GtkToggleButton *tb, gpointer data)
{
    SEState *s = (SEState *)data;
    gboolean on = gtk_toggle_button_get_active(tb);
    gtk_widget_set_sensitive(s->bg_btn, on);
    save_panel_to_store(s);
}

static void on_color_set(GtkColorButton *cb, gpointer data)
{
    (void)cb;
    save_panel_to_store((SEState *)data);
}

static void on_toggle_changed(GtkToggleButton *tb, gpointer data)
{
    (void)tb;
    save_panel_to_store((SEState *)data);
}

/* ------------------------------------------------------------------ */
/* Response signal handler                                            */
/* ------------------------------------------------------------------ */

static void on_response(GtkDialog *dialog, gint resp, gpointer data)
{
    SEState *s = (SEState *)data;

    if (resp == 1) {   /* Save — keep dialog open */
        stylestore_save_user();
        s->changed = FALSE;
        if (s->on_apply) s->on_apply();
        return;
    }

    if (resp == GTK_RESPONSE_ACCEPT) {   /* Save and Close */
        stylestore_save_user();
        s->changed = FALSE;
        if (s->on_apply) s->on_apply();
        gtk_widget_hide(GTK_WIDGET(dialog));
        return;
    }

    /* Close button or window-delete */
    if (s->changed) {
        GtkWidget *ask = gtk_message_dialog_new(
            GTK_WINDOW(dialog),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "Save style changes?");
        gint ans = gtk_dialog_run(GTK_DIALOG(ask));
        gtk_widget_destroy(ask);
        if (ans == GTK_RESPONSE_YES) {
            stylestore_save_user();
            if (s->on_apply) s->on_apply();
        }
    }
    s->changed = FALSE;
    gtk_widget_hide(GTK_WIDGET(dialog));
}

/* ------------------------------------------------------------------ */
/* Build dialog                                                       */
/* ------------------------------------------------------------------ */

void styleeditor_show(GtkWidget *parent, SEApplyFn on_apply)
{
    static SEState *s_instance = NULL;

    if (s_instance) {
        s_instance->on_apply = on_apply;
        s_instance->changed  = FALSE;
        gtk_window_present(GTK_WINDOW(s_instance->dialog));
        return;
    }

    SEState *s = g_new0(SEState, 1);
    s_instance = s;
    s->sel_block = -1;
    s->sel_entry = -1;
    s->theme_paths = g_ptr_array_new_with_free_func(g_free);
    s->on_apply = on_apply;

    /* Create dialog */
    s->dialog = gtk_dialog_new_with_buttons(
        T("dlg.StyleConfig.title", "Style Configurator"),
        parent ? GTK_WINDOW(parent) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        TM("cmd.41006",            "_Save"),             1,
        TM("dlg.StyleConfig.2301", "_Save and Close"),   GTK_RESPONSE_ACCEPT,
        TM("dlg.Find.2",           "_Close"),            GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(s->dialog), 820, 560);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(s->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    /* ---- Theme row ---- */
    GtkWidget *theme_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(content), theme_hbox, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(theme_hbox),
                       gtk_label_new(T("dlg.StyleConfig.2306", "Theme:")), FALSE, FALSE, 0);
    s->theme_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(s->theme_combo, TRUE);
    gtk_box_pack_start(GTK_BOX(theme_hbox), s->theme_combo, TRUE, TRUE, 0);

    /* ---- Main paned ---- */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(content), paned, TRUE, TRUE, 4);

    /* Left: language list */
    GtkWidget *lang_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lang_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(lang_scroll, 180, -1);
    s->lang_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(s->lang_list),
                                    GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(lang_scroll), s->lang_list);
    gtk_paned_pack1(GTK_PANED(paned), lang_scroll, FALSE, FALSE);

    /* Right: vertical split — style list on top, attribute panel below */
    GtkWidget *right_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack2(GTK_PANED(paned), right_paned, TRUE, FALSE);

    GtkWidget *style_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(style_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(style_scroll, -1, 160);
    s->style_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(s->style_list),
                                    GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(style_scroll), s->style_list);
    gtk_paned_pack1(GTK_PANED(right_paned), style_scroll, FALSE, FALSE);

    /* Attribute panel */
    GtkWidget *attr_frame = gtk_frame_new("Style attributes");
    gtk_paned_pack2(GTK_PANED(right_paned), attr_frame, TRUE, FALSE);
    GtkWidget *attr_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(attr_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(attr_grid), 8);
    gtk_widget_set_margin_start(attr_grid, 10);
    gtk_widget_set_margin_end(attr_grid, 10);
    gtk_widget_set_margin_top(attr_grid, 8);
    gtk_widget_set_margin_bottom(attr_grid, 8);
    gtk_container_add(GTK_CONTAINER(attr_frame), attr_grid);

    int row = 0;

    /* Font */
    gtk_grid_attach(GTK_GRID(attr_grid), gtk_label_new(T("dlg.StyleConfig.2208", "Font:")), 0, row, 1, 1);
    s->font_btn = gtk_font_button_new();
    g_object_set(s->font_btn, "use-font", TRUE, "use-size", TRUE, NULL);
    gtk_widget_set_hexpand(s->font_btn, TRUE);
    gtk_grid_attach(GTK_GRID(attr_grid), s->font_btn, 1, row, 3, 1);
    row++;

    /* Foreground */
    gtk_grid_attach(GTK_GRID(attr_grid), gtk_label_new(T("dlg.StyleConfig.2206", "Foreground:")), 0, row, 1, 1);
    s->fg_check = gtk_check_button_new_with_label(T("dlg.StyleConfig.2212", "Enable"));
    gtk_grid_attach(GTK_GRID(attr_grid), s->fg_check, 1, row, 1, 1);
    s->fg_btn = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(s->fg_btn), FALSE);
    gtk_widget_set_sensitive(s->fg_btn, FALSE);
    gtk_grid_attach(GTK_GRID(attr_grid), s->fg_btn, 2, row, 1, 1);
    row++;

    /* Background */
    gtk_grid_attach(GTK_GRID(attr_grid), gtk_label_new(T("dlg.StyleConfig.2207", "Background:")), 0, row, 1, 1);
    s->bg_check = gtk_check_button_new_with_label(T("dlg.StyleConfig.2212", "Enable"));
    gtk_grid_attach(GTK_GRID(attr_grid), s->bg_check, 1, row, 1, 1);
    s->bg_btn = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(s->bg_btn), FALSE);
    gtk_widget_set_sensitive(s->bg_btn, FALSE);
    gtk_grid_attach(GTK_GRID(attr_grid), s->bg_btn, 2, row, 1, 1);
    row++;

    /* Bold / Italic / Underline */
    s->bold_check      = gtk_check_button_new_with_label(T("dlg.StyleConfig.2204", "Bold"));
    s->italic_check    = gtk_check_button_new_with_label(T("dlg.StyleConfig.2205", "Italic"));
    s->underline_check = gtk_check_button_new_with_label(T("dlg.StyleConfig.2218", "Underline"));
    GtkWidget *style_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(style_hbox), s->bold_check,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(style_hbox), s->italic_check,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(style_hbox), s->underline_check, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(attr_grid), style_hbox, 1, row, 3, 1);
    row++;

    /* Preview */
    gtk_grid_attach(GTK_GRID(attr_grid), gtk_label_new(T("dlg.StyleConfig.2213", "Preview:")), 0, row, 1, 1);
    s->preview_label = gtk_label_new("  AaBbCcDd 123  ");
    gtk_label_set_use_markup(GTK_LABEL(s->preview_label), TRUE);
    gtk_widget_set_halign(s->preview_label, GTK_ALIGN_START);
    GtkWidget *preview_frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(preview_frame), s->preview_label);
    gtk_grid_attach(GTK_GRID(attr_grid), preview_frame, 1, row, 3, 1);

    /* ---- Connect signals ---- */
    g_signal_connect(s->dialog, "response",     G_CALLBACK(on_response),              s);
    g_signal_connect(s->dialog, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    g_signal_connect(s->theme_combo, "changed",
                     G_CALLBACK(on_theme_changed), s);
    g_signal_connect(s->lang_list, "row-selected",
                     G_CALLBACK(on_lang_row_selected), s);
    g_signal_connect(s->style_list, "row-selected",
                     G_CALLBACK(on_style_row_selected), s);
    g_signal_connect(s->font_btn, "font-set",
                     G_CALLBACK(on_font_set), s);
    g_signal_connect(s->fg_check, "toggled",
                     G_CALLBACK(on_fg_check_toggled), s);
    g_signal_connect(s->bg_check, "toggled",
                     G_CALLBACK(on_bg_check_toggled), s);
    g_signal_connect(s->fg_btn, "color-set",
                     G_CALLBACK(on_color_set), s);
    g_signal_connect(s->bg_btn, "color-set",
                     G_CALLBACK(on_color_set), s);
    g_signal_connect(s->bold_check,      "toggled", G_CALLBACK(on_toggle_changed), s);
    g_signal_connect(s->italic_check,    "toggled", G_CALLBACK(on_toggle_changed), s);
    g_signal_connect(s->underline_check, "toggled", G_CALLBACK(on_toggle_changed), s);

    /* ---- Populate initial data ---- */
    populate_theme_combo(s);  /* triggers on_theme_changed → populate_lang_list */
    s->changed = FALSE;       /* reset: on_theme_changed fired during init, not a real edit */
    gtk_widget_show_all(s->dialog);
}
