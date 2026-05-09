#include "workspace.h"
#include "editor.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* TreeStore columns                                                  */
/* ------------------------------------------------------------------ */

enum {
    COL_NAME = 0,
    COL_PATH,
    COL_IS_DIR,
    N_COLS
};

/* Sentinel value stored in COL_PATH for unloaded directory placeholders */
#define DUMMY_PATH "##dummy##"

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

static GtkWidget    *s_panel    = NULL;
static GtkWidget    *s_tree     = NULL;
static GtkWidget    *s_path_lbl = NULL;
static GtkTreeStore *s_store    = NULL;
static GtkWidget    *s_window   = NULL;  /* parent for dialogs */
static char         *s_root     = NULL;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static gboolean has_only_dummy(GtkTreeIter *parent)
{
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(s_store), &child, parent))
        return FALSE;
    char *path = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(s_store), &child, COL_PATH, &path, -1);
    gboolean result = (path && strcmp(path, DUMMY_PATH) == 0);
    g_free(path);
    return result;
}

static void add_dummy(GtkTreeIter *parent)
{
    GtkTreeIter dummy;
    gtk_tree_store_append(s_store, &dummy, parent);
    gtk_tree_store_set(s_store, &dummy,
                       COL_NAME,   "",
                       COL_PATH,   DUMMY_PATH,
                       COL_IS_DIR, FALSE,
                       -1);
}

/* Fill children of parent_iter from the filesystem directory at path. */
static void populate_dir(GtkTreeIter *parent_iter, const char *path)
{
    GFile  *dir = g_file_new_for_path(path);
    GError *err = NULL;
    GFileEnumerator *en = g_file_enumerate_children(
        dir,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NONE, NULL, &err);
    g_object_unref(dir);

    if (!en) {
        if (err) g_error_free(err);
        return;
    }

    /* Collect entries first so we can sort: dirs before files */
    GSList *dirs = NULL, *files = NULL;

    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
        const char *name = g_file_info_get_name(info);
        if (name[0] == '.') { g_object_unref(info); continue; } /* skip hidden */
        GFileType type = g_file_info_get_file_type(info);
        char *child_path = g_build_filename(path, name, NULL);
        /* Store as "name\0path\0is_dir" packed in a single allocation */
        gboolean is_dir = (type == G_FILE_TYPE_DIRECTORY);
        char *entry = g_strdup_printf("%s%c%s%c%d",
                                      name, '\0', child_path, '\0', (int)is_dir);
        if (is_dir) dirs  = g_slist_prepend(dirs,  entry);
        else        files = g_slist_prepend(files, entry);
        g_free(child_path);
        g_object_unref(info);
    }
    g_file_enumerator_close(en, NULL, NULL);
    g_object_unref(en);

    /* Sort both lists alphabetically */
    dirs  = g_slist_sort(dirs,  (GCompareFunc)g_ascii_strcasecmp);
    files = g_slist_sort(files, (GCompareFunc)g_ascii_strcasecmp);

    /* Insert: directories first, then files */
    GSList *lists[2] = { dirs, files };
    for (int l = 0; l < 2; l++) {
        for (GSList *node = lists[l]; node; node = node->next) {
            char *entry   = (char *)node->data;
            const char *nm = entry;
            const char *fp = entry + strlen(nm) + 1;
            gboolean    id = (gboolean)atoi(fp + strlen(fp) + 1);

            GtkTreeIter iter;
            gtk_tree_store_append(s_store, &iter, parent_iter);
            gtk_tree_store_set(s_store, &iter,
                               COL_NAME,   nm,
                               COL_PATH,   fp,
                               COL_IS_DIR, id,
                               -1);
            if (id) add_dummy(&iter); /* expander placeholder */
        }
    }

    g_slist_free_full(dirs,  g_free);
    g_slist_free_full(files, g_free);
}

/* ------------------------------------------------------------------ */
/* Cell renderer — folder/file icon via icon theme                    */
/* ------------------------------------------------------------------ */

static void render_icon(GtkTreeViewColumn *col, GtkCellRenderer *cell,
                        GtkTreeModel *model, GtkTreeIter *iter, gpointer d)
{
    (void)col; (void)d;
    gboolean is_dir;
    char *path = NULL;
    gtk_tree_model_get(model, iter, COL_IS_DIR, &is_dir, COL_PATH, &path, -1);
    gboolean is_dummy = (path && strcmp(path, DUMMY_PATH) == 0);
    g_free(path);
    if (is_dummy) {
        g_object_set(cell, "icon-name", NULL, NULL);
        return;
    }
    g_object_set(cell, "icon-name", is_dir ? "folder" : "text-x-generic", NULL);
}

/* ------------------------------------------------------------------ */
/* Signal handlers                                                    */
/* ------------------------------------------------------------------ */

static void on_row_expanded(GtkTreeView *tv, GtkTreeIter *iter,
                             GtkTreePath *tp, gpointer d)
{
    (void)tv; (void)tp; (void)d;
    if (!has_only_dummy(iter)) return;

    /* Remove dummy placeholder */
    GtkTreeIter child;
    gtk_tree_model_iter_children(GTK_TREE_MODEL(s_store), &child, iter);
    gtk_tree_store_remove(s_store, &child);

    char *dir_path = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(s_store), iter, COL_PATH, &dir_path, -1);
    if (dir_path) {
        populate_dir(iter, dir_path);
        g_free(dir_path);
    }
}

static void on_row_activated(GtkTreeView *tv, GtkTreePath *tp,
                              GtkTreeViewColumn *col, gpointer d)
{
    (void)tv; (void)col; (void)d;
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(s_store), &iter, tp)) return;

    gboolean is_dir;
    char    *fpath = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(s_store), &iter,
                       COL_IS_DIR, &is_dir,
                       COL_PATH,   &fpath, -1);
    if (!is_dir && fpath)
        editor_open_path(fpath);
    g_free(fpath);
}

static void on_close_clicked(GtkButton *btn, gpointer d)
{
    (void)btn; (void)d;
    workspace_set_visible(FALSE);
}

static void on_open_folder_clicked(GtkButton *btn, gpointer d)
{
    (void)btn; (void)d;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Open Folder as Workspace",
        s_window ? GTK_WINDOW(s_window) : NULL,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT,
        NULL);

    if (s_root)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), s_root);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        workspace_set_folder(folder);
        g_free(folder);
    }
    gtk_widget_destroy(dlg);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

GtkWidget *workspace_init(GtkWidget *parent_window)
{
    s_window = parent_window;

    s_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(s_panel, 220, -1);

    /* Header row */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    GtkWidget *title = gtk_label_new("Folder as Workspace");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 6);

    GtkWidget *open_btn = gtk_button_new_with_label("…");
    gtk_button_set_relief(GTK_BUTTON(open_btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(open_btn, "Open Folder…");
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_folder_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header), open_btn, FALSE, FALSE, 0);

    GtkWidget *close_btn = gtk_button_new_with_label("×");
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header), close_btn, FALSE, FALSE, 2);

    gtk_box_pack_start(GTK_BOX(s_panel), header, FALSE, FALSE, 0);

    /* Current path label */
    s_path_lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(s_path_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(s_path_lbl), PANGO_ELLIPSIZE_START);
    gtk_widget_set_margin_start(s_path_lbl, 6);
    gtk_widget_set_margin_end(s_path_lbl, 6);
    gtk_widget_set_margin_bottom(s_path_lbl, 2);
    gtk_box_pack_start(GTK_BOX(s_panel), s_path_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(s_panel),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);

    /* Tree model */
    s_store = gtk_tree_store_new(N_COLS,
                                 G_TYPE_STRING,   /* COL_NAME   */
                                 G_TYPE_STRING,   /* COL_PATH   */
                                 G_TYPE_BOOLEAN); /* COL_IS_DIR */

    /* Tree view */
    s_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(s_tree), FALSE);
    gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(s_tree), FALSE);

    GtkTreeViewColumn *col = gtk_tree_view_column_new();

    GtkCellRenderer *icon_rend = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, icon_rend, FALSE);
    gtk_tree_view_column_set_cell_data_func(col, icon_rend,
                                            render_icon, NULL, NULL);

    GtkCellRenderer *text_rend = gtk_cell_renderer_text_new();
    g_object_set(text_rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_column_pack_start(col, text_rend, TRUE);
    gtk_tree_view_column_add_attribute(col, text_rend, "text", COL_NAME);

    gtk_tree_view_append_column(GTK_TREE_VIEW(s_tree), col);

    g_signal_connect(s_tree, "row-expanded",  G_CALLBACK(on_row_expanded),  NULL);
    g_signal_connect(s_tree, "row-activated", G_CALLBACK(on_row_activated), NULL);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), s_tree);
    gtk_box_pack_start(GTK_BOX(s_panel), scroll, TRUE, TRUE, 0);

    gtk_widget_hide(s_panel);
    return s_panel;
}

void workspace_set_folder(const char *path)
{
    if (!s_store || !path) return;

    g_free(s_root);
    s_root = g_strdup(path);

    /* Update path label */
    if (s_path_lbl)
        gtk_label_set_text(GTK_LABEL(s_path_lbl), path);

    /* Rebuild tree from scratch */
    gtk_tree_store_clear(s_store);

    /* Root node */
    GtkTreeIter root;
    const char *basename = strrchr(path, '/');
    gtk_tree_store_append(s_store, &root, NULL);
    gtk_tree_store_set(s_store, &root,
                       COL_NAME,   basename ? basename + 1 : path,
                       COL_PATH,   path,
                       COL_IS_DIR, TRUE,
                       -1);

    populate_dir(&root, path);

    /* Auto-expand the root node */
    GtkTreePath *tp = gtk_tree_model_get_path(GTK_TREE_MODEL(s_store), &root);
    gtk_tree_view_expand_row(GTK_TREE_VIEW(s_tree), tp, FALSE);
    gtk_tree_path_free(tp);
}

void workspace_set_visible(gboolean v)
{
    if (!s_panel) return;
    if (v)
        gtk_widget_show(s_panel);
    else
        gtk_widget_hide(s_panel);
}

gboolean workspace_is_visible(void)
{
    if (!s_panel) return FALSE;
    return gtk_widget_get_visible(s_panel);
}
