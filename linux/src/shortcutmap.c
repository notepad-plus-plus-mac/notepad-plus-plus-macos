#include "shortcutmap.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Shortcut table                                                      */
/* ------------------------------------------------------------------ */

static ShortcutEntry s_table[] = {
    /* File */
    { "cmd.new",        "New",                    "File",   GDK_KEY_n,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.open",       "Open",                   "File",   GDK_KEY_o,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.save",       "Save",                   "File",   GDK_KEY_s,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.saveas",     "Save As",                "File",   GDK_KEY_s,            GDK_CONTROL_MASK|GDK_SHIFT_MASK,     0,0,NULL,NULL },
    { "cmd.close",      "Close",                  "File",   GDK_KEY_w,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.quit",       "Quit",                   "File",   GDK_KEY_q,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    /* Edit */
    { "cmd.undo",       "Undo",                   "Edit",   GDK_KEY_z,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.redo",       "Redo",                   "Edit",   GDK_KEY_z,            GDK_CONTROL_MASK|GDK_SHIFT_MASK,     0,0,NULL,NULL },
    { "cmd.cut",        "Cut",                    "Edit",   GDK_KEY_x,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.copy",       "Copy",                   "Edit",   GDK_KEY_c,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.paste",      "Paste",                  "Edit",   GDK_KEY_v,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.selall",     "Select All",             "Edit",   GDK_KEY_a,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.line.dup",   "Duplicate Line",         "Edit",   GDK_KEY_d,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.line.del",   "Delete Line",            "Edit",   GDK_KEY_l,            GDK_CONTROL_MASK|GDK_SHIFT_MASK,     0,0,NULL,NULL },
    { "cmd.line.up",    "Move Line Up",           "Edit",   GDK_KEY_Up,           GDK_CONTROL_MASK|GDK_SHIFT_MASK,     0,0,NULL,NULL },
    { "cmd.line.down",  "Move Line Down",         "Edit",   GDK_KEY_Down,         GDK_CONTROL_MASK|GDK_SHIFT_MASK,     0,0,NULL,NULL },
    { "cmd.line.insabove","Insert Blank Line Above","Edit", GDK_KEY_Return,       GDK_CONTROL_MASK|GDK_MOD1_MASK,      0,0,NULL,NULL },
    { "cmd.line.insbelow","Insert Blank Line Below","Edit", GDK_KEY_Return,       GDK_CONTROL_MASK|GDK_SHIFT_MASK,     0,0,NULL,NULL },
    { "cmd.comment.line", "Toggle Line Comment",  "Edit",   GDK_KEY_k,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.comment.block","Toggle Block Comment", "Edit",   GDK_KEY_k,            GDK_CONTROL_MASK|GDK_SHIFT_MASK,     0,0,NULL,NULL },
    /* Search */
    { "cmd.find",       "Find",                   "Search", GDK_KEY_f,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.replace",    "Replace",                "Search", GDK_KEY_h,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.goto",       "Go To Line",             "Search", GDK_KEY_g,            GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.brace",      "Go to Matching Brace",   "Search", GDK_KEY_bracketright, GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.bm.toggle",  "Toggle Bookmark",        "Search", GDK_KEY_F2,           GDK_CONTROL_MASK,                    0,0,NULL,NULL },
    { "cmd.bm.next",    "Next Bookmark",          "Search", GDK_KEY_F2,           0,                                   0,0,NULL,NULL },
    { "cmd.bm.prev",    "Previous Bookmark",      "Search", GDK_KEY_F2,           GDK_SHIFT_MASK,                      0,0,NULL,NULL },
};

static const int s_count = (int)(sizeof(s_table) / sizeof(s_table[0]));

ShortcutEntry *shortcut_table(int *count)
{
    if (count) *count = s_count;
    return s_table;
}

ShortcutEntry *shortcut_find(const char *id)
{
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_table[i].id, id) == 0)
            return &s_table[i];
    return NULL;
}

void shortcut_register(const char *id, GtkWidget *widget, GtkAccelGroup *group)
{
    ShortcutEntry *e = shortcut_find(id);
    if (!e) return;
    e->widget = widget;
    e->group  = group;
}

/* ------------------------------------------------------------------ */
/* Active key for an entry (current if set, otherwise default)        */
/* ------------------------------------------------------------------ */

static guint entry_key(const ShortcutEntry *e)
{
    return e->current_key ? e->current_key : e->default_key;
}

static GdkModifierType entry_mod(const ShortcutEntry *e)
{
    return e->current_key ? e->current_mod : e->default_mod;
}

/* ------------------------------------------------------------------ */
/* XML load                                                            */
/* ------------------------------------------------------------------ */

typedef struct { ShortcutEntry *entry; } ParseCtx;

static void xml_start(GMarkupParseContext *ctx, const gchar *el,
                      const gchar **attr_names, const gchar **attr_vals,
                      gpointer user_data, GError **err)
{
    (void)ctx; (void)user_data; (void)err;
    if (strcmp(el, "Shortcut") != 0) return;

    const char *id  = NULL;
    const char *key_s = NULL;
    const char *ctrl_s = NULL, *alt_s = NULL, *shift_s = NULL;

    for (int i = 0; attr_names[i]; i++) {
        if      (strcmp(attr_names[i], "id")    == 0) id    = attr_vals[i];
        else if (strcmp(attr_names[i], "key")   == 0) key_s = attr_vals[i];
        else if (strcmp(attr_names[i], "ctrl")  == 0) ctrl_s  = attr_vals[i];
        else if (strcmp(attr_names[i], "alt")   == 0) alt_s   = attr_vals[i];
        else if (strcmp(attr_names[i], "shift") == 0) shift_s = attr_vals[i];
    }

    if (!id || !key_s) return;
    ShortcutEntry *e = shortcut_find(id);
    if (!e) return;

    guint key = (guint)atoi(key_s);
    GdkModifierType mod = 0;
    if (ctrl_s  && strcmp(ctrl_s,  "yes") == 0) mod |= GDK_CONTROL_MASK;
    if (alt_s   && strcmp(alt_s,   "yes") == 0) mod |= GDK_MOD1_MASK;
    if (shift_s && strcmp(shift_s, "yes") == 0) mod |= GDK_SHIFT_MASK;

    e->current_key = key;
    e->current_mod = mod;
}

static GMarkupParser s_xml_parser = { xml_start, NULL, NULL, NULL, NULL };

void shortcut_load(void)
{
    gchar *path = g_build_filename(g_get_home_dir(), ".config", "notetux",
                                   "shortcuts.xml", NULL);
    gchar *xml  = NULL;
    if (!g_file_get_contents(path, &xml, NULL, NULL)) {
        g_free(path);
        return;
    }
    GMarkupParseContext *ctx = g_markup_parse_context_new(&s_xml_parser, 0, NULL, NULL);
    g_markup_parse_context_parse(ctx, xml, -1, NULL);
    g_markup_parse_context_free(ctx);
    g_free(xml);
    g_free(path);
}

/* ------------------------------------------------------------------ */
/* XML save                                                            */
/* ------------------------------------------------------------------ */

void shortcut_save(void)
{
    gchar *dir  = g_build_filename(g_get_home_dir(), ".config", "notetux", NULL);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    gchar *path = g_build_filename(g_get_home_dir(), ".config", "notetux",
                                   "shortcuts.xml", NULL);
    GString *buf = g_string_new(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<NotepadPlus>\n"
        "    <InternalCommands>\n");

    for (int i = 0; i < s_count; i++) {
        const ShortcutEntry *e = &s_table[i];
        guint           k = entry_key(e);
        GdkModifierType m = entry_mod(e);
        g_string_append_printf(buf,
            "        <Shortcut id=\"%s\" key=\"%u\""
            " ctrl=\"%s\" alt=\"%s\" shift=\"%s\" />\n",
            e->id, k,
            (m & GDK_CONTROL_MASK) ? "yes" : "no",
            (m & GDK_MOD1_MASK)    ? "yes" : "no",
            (m & GDK_SHIFT_MASK)   ? "yes" : "no");
    }

    g_string_append(buf, "    </InternalCommands>\n</NotepadPlus>\n");
    g_file_set_contents(path, buf->str, (gssize)buf->len, NULL);
    g_string_free(buf, TRUE);
    g_free(path);
}

/* ------------------------------------------------------------------ */
/* Shortcut label (human-readable, e.g. "Ctrl+N")                    */
/* ------------------------------------------------------------------ */

static char *shortcut_label_str(guint key, GdkModifierType mod)
{
    if (!key) return g_strdup("(none)");
    return gtk_accelerator_get_label(key, mod);
}

/* ------------------------------------------------------------------ */
/* Key-capture dialog                                                  */
/* ------------------------------------------------------------------ */

typedef struct { guint key; GdkModifierType mod; gboolean ok; } CaptureResult;

static gboolean on_capture_key(GtkWidget *dlg, GdkEventKey *ev, gpointer data)
{
    /* Ignore standalone modifier keys */
    switch (ev->keyval) {
        case GDK_KEY_Control_L: case GDK_KEY_Control_R:
        case GDK_KEY_Shift_L:   case GDK_KEY_Shift_R:
        case GDK_KEY_Alt_L:     case GDK_KEY_Alt_R:
        case GDK_KEY_Super_L:   case GDK_KEY_Super_R:
        case GDK_KEY_Meta_L:    case GDK_KEY_Meta_R:
            return FALSE;
        default: break;
    }
    if (ev->keyval == GDK_KEY_Escape) {
        gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_CANCEL);
        return TRUE;
    }
    CaptureResult *r = (CaptureResult *)data;
    r->key = ev->keyval;
    r->mod = ev->state & gtk_accelerator_get_default_mod_mask();
    r->ok  = TRUE;
    gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    return TRUE;
}

static gboolean capture_new_shortcut(GtkWidget *parent, CaptureResult *out)
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Assign Shortcut",
        GTK_WINDOW(parent),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        NULL);
    gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);

    GtkWidget *label = gtk_label_new("Press a new key combination…\n(Escape to cancel)");
    gtk_widget_set_margin_start(label, 24);
    gtk_widget_set_margin_end(label, 24);
    gtk_widget_set_margin_top(label, 16);
    gtk_widget_set_margin_bottom(label, 16);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), label);
    gtk_widget_show_all(dlg);

    out->ok = FALSE;
    g_signal_connect(dlg, "key-press-event", G_CALLBACK(on_capture_key), out);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    return out->ok;
}

/* ------------------------------------------------------------------ */
/* Mapper dialog                                                       */
/* ------------------------------------------------------------------ */

enum { COL_CATEGORY = 0, COL_NAME, COL_SHORTCUT, COL_INDEX, N_COLS };

static GtkWidget *s_mapper_dlg  = NULL;
static GtkWidget *s_mapper_view = NULL;

static void refresh_store(GtkListStore *store)
{
    gtk_list_store_clear(store);
    for (int i = 0; i < s_count; i++) {
        const ShortcutEntry *e = &s_table[i];
        char *sc = shortcut_label_str(entry_key(e), entry_mod(e));
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it,
            COL_CATEGORY, e->category,
            COL_NAME,     e->label,
            COL_SHORTCUT, sc,
            COL_INDEX,    i,
            -1);
        g_free(sc);
    }
}

static void on_row_activated(GtkTreeView *tv, GtkTreePath *path,
                              GtkTreeViewColumn *col, gpointer data)
{
    (void)col;
    GtkListStore *store = (GtkListStore *)data;
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, path)) return;

    gint idx;
    gtk_tree_model_get(GTK_TREE_MODEL(store), &it, COL_INDEX, &idx, -1);
    if (idx < 0 || idx >= s_count) return;
    ShortcutEntry *e = &s_table[idx];

    CaptureResult r = {0};
    if (!capture_new_shortcut(s_mapper_dlg, &r)) return;

    /* Apply: remove old accelerator, add new one */
    if (e->widget && e->group) {
        gtk_widget_remove_accelerator(e->widget, e->group,
                                      entry_key(e), entry_mod(e));
        if (r.key) {
            gtk_widget_add_accelerator(e->widget, "activate", e->group,
                                       r.key, r.mod, GTK_ACCEL_VISIBLE);
        }
    }
    e->current_key = r.key;
    e->current_mod = r.mod;

    /* Update the row */
    char *sc = shortcut_label_str(r.key, r.mod);
    gtk_list_store_set(store, &it, COL_SHORTCUT, sc, -1);
    g_free(sc);

    shortcut_save();
}

static void on_reset_selected(GtkButton *btn, gpointer data)
{
    (void)btn;
    GtkListStore *store = (GtkListStore *)data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(s_mapper_view));
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, NULL, &it)) return;

    gint idx;
    gtk_tree_model_get(GTK_TREE_MODEL(store), &it, COL_INDEX, &idx, -1);
    if (idx < 0 || idx >= s_count) return;
    ShortcutEntry *e = &s_table[idx];

    if (e->widget && e->group) {
        gtk_widget_remove_accelerator(e->widget, e->group,
                                      entry_key(e), entry_mod(e));
        gtk_widget_add_accelerator(e->widget, "activate", e->group,
                                   e->default_key, e->default_mod,
                                   GTK_ACCEL_VISIBLE);
    }
    e->current_key = 0;
    e->current_mod = 0;

    char *sc = shortcut_label_str(e->default_key, e->default_mod);
    gtk_list_store_set(store, &it, COL_SHORTCUT, sc, -1);
    g_free(sc);

    shortcut_save();
}

static void on_reset_all(GtkButton *btn, gpointer data)
{
    (void)btn;
    GtkListStore *store = (GtkListStore *)data;
    for (int i = 0; i < s_count; i++) {
        ShortcutEntry *e = &s_table[i];
        if (e->current_key && e->widget && e->group) {
            gtk_widget_remove_accelerator(e->widget, e->group,
                                          e->current_key, e->current_mod);
            gtk_widget_add_accelerator(e->widget, "activate", e->group,
                                       e->default_key, e->default_mod,
                                       GTK_ACCEL_VISIBLE);
        }
        e->current_key = 0;
        e->current_mod = 0;
    }
    refresh_store(store);
    shortcut_save();
}

static void on_mapper_response(GtkDialog *dlg, gint resp, gpointer d)
{
    (void)resp; (void)d;
    gtk_widget_hide(GTK_WIDGET(dlg));
}

void shortcut_mapper_show(GtkWidget *parent)
{
    if (s_mapper_dlg) {
        gtk_window_set_transient_for(GTK_WINDOW(s_mapper_dlg), GTK_WINDOW(parent));
        gtk_window_present(GTK_WINDOW(s_mapper_dlg));
        return;
    }

    /* Create list store */
    GtkListStore *store = gtk_list_store_new(N_COLS,
        G_TYPE_STRING,  /* category */
        G_TYPE_STRING,  /* name     */
        G_TYPE_STRING,  /* shortcut */
        G_TYPE_INT);    /* index    */
    refresh_store(store);

    /* Sort by category, then name */
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);
    gtk_tree_sortable_set_sort_column_id(sortable, COL_CATEGORY, GTK_SORT_ASCENDING);

    /* Tree view */
    s_mapper_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    GtkCellRenderer *tr = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(s_mapper_view),
        gtk_tree_view_column_new_with_attributes("Category", tr, "text", COL_CATEGORY, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(s_mapper_view),
        gtk_tree_view_column_new_with_attributes("Command",  tr, "text", COL_NAME,     NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(s_mapper_view),
        gtk_tree_view_column_new_with_attributes("Shortcut", tr, "text", COL_SHORTCUT, NULL));

    /* Make columns resizable and set initial widths */
    GList *cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(s_mapper_view));
    int widths[] = { 80, 220, 150 };
    int wi = 0;
    for (GList *l = cols; l; l = l->next, wi++) {
        gtk_tree_view_column_set_resizable(GTK_TREE_VIEW_COLUMN(l->data), TRUE);
        gtk_tree_view_column_set_min_width(GTK_TREE_VIEW_COLUMN(l->data), widths[wi]);
    }
    g_list_free(cols);

    g_signal_connect(s_mapper_view, "row-activated",
                     G_CALLBACK(on_row_activated), store);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 500, 360);
    gtk_container_add(GTK_CONTAINER(scroll), s_mapper_view);

    /* Button bar below the list */
    GtkWidget *reset_sel = gtk_button_new_with_mnemonic("_Reset Selected");
    GtkWidget *reset_all = gtk_button_new_with_mnemonic("Reset _All");
    GtkWidget *btn_bar   = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(btn_bar), GTK_BUTTONBOX_START);
    gtk_box_set_spacing(GTK_BOX(btn_bar), 4);
    gtk_container_add(GTK_CONTAINER(btn_bar), reset_sel);
    gtk_container_add(GTK_CONTAINER(btn_bar), reset_all);

    GtkWidget *note = gtk_label_new("Double-click a row to assign a new shortcut.");
    gtk_widget_set_halign(note, GTK_ALIGN_START);

    g_signal_connect(reset_sel, "clicked", G_CALLBACK(on_reset_selected), store);
    g_signal_connect(reset_all, "clicked", G_CALLBACK(on_reset_all),      store);

    /* Dialog */
    s_mapper_dlg = gtk_dialog_new_with_buttons(
        "Shortcut Mapper",
        GTK_WINDOW(parent),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_resizable(GTK_WINDOW(s_mapper_dlg), TRUE);

    GtkWidget *ca = gtk_dialog_get_content_area(GTK_DIALOG(s_mapper_dlg));
    gtk_box_set_spacing(GTK_BOX(ca), 6);
    gtk_container_set_border_width(GTK_CONTAINER(ca), 8);
    gtk_container_add(GTK_CONTAINER(ca), note);
    gtk_container_add(GTK_CONTAINER(ca), scroll);
    gtk_container_add(GTK_CONTAINER(ca), btn_bar);

    g_signal_connect(s_mapper_dlg, "response", G_CALLBACK(on_mapper_response), NULL);
    gtk_widget_show_all(s_mapper_dlg);
}
