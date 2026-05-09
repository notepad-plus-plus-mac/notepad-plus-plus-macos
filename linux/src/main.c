#include <gtk/gtk.h>
#include "sci_c.h"
#include "editor.h"
#include "encoding.h"
#include "shortcutmap.h"
#include "prefs.h"
#include "statusbar.h"
#include "findreplace.h"
#include "findinfiles.h"
#include "columneditor.h"
#include "toolbar.h"
#include "styleeditor.h"
#include "lexer.h"
#include "udl.h"
#include "i18n.h"
#include "session.h"
#include "backup.h"
#include "macro.h"
#include "doclist.h"
#include "workspace.h"
#include "funclist.h"

/* Set to TRUE in main() when no file arguments are given; read in on_activate. */
static gboolean s_restore_session = FALSE;

/* ------------------------------------------------------------------ */
/* Menu callbacks                                                      */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Recent files                                                        */
/* ------------------------------------------------------------------ */

#define MAX_RECENT 10

static GPtrArray  *s_recent_files = NULL;   /* GPtrArray of g_strdup'd paths */
static GtkWidget  *s_recent_menu  = NULL;   /* the GtkMenu inside the submenu */
static GtkWidget  *s_recent_item  = NULL;   /* the top-level "Recent Files" item */

static char *recent_file_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "npp", "recentfiles.txt", NULL);
}

static void recent_save(void)
{
    GString *buf = g_string_new(NULL);
    for (guint i = 0; i < s_recent_files->len; i++)
        g_string_append_printf(buf, "%s\n", (char *)s_recent_files->pdata[i]);
    gchar *path = recent_file_path();
    g_file_set_contents(path, buf->str, (gssize)buf->len, NULL);
    g_free(path);
    g_string_free(buf, TRUE);
}

static void recent_load(void)
{
    gchar *path = recent_file_path();
    gchar *contents = NULL;
    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (int i = 0; lines[i] && s_recent_files->len < MAX_RECENT; i++) {
            if (lines[i][0] != '\0')
                g_ptr_array_add(s_recent_files, g_strdup(lines[i]));
        }
        g_strfreev(lines);
        g_free(contents);
    }
    g_free(path);
}

static void cb_open_recent(GtkMenuItem *item, gpointer data)
{
    (void)item;
    editor_open_path((const char *)data);
}

static void cb_clear_recent(GtkMenuItem *i, gpointer d);

static void recent_rebuild_menu(void)
{
    /* Remove all children */
    GList *children = gtk_container_get_children(GTK_CONTAINER(s_recent_menu));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    if (s_recent_files->len == 0) {
        GtkWidget *empty = gtk_menu_item_new_with_label(T("menu.recent.empty", "(empty)"));
        gtk_widget_set_sensitive(empty, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(s_recent_menu), empty);
    } else {
        for (guint i = 0; i < s_recent_files->len; i++) {
            const char *fpath = (const char *)s_recent_files->pdata[i];
            gchar *label = g_path_get_basename(fpath);
            GtkWidget *mi = gtk_menu_item_new_with_label(label);
            g_free(label);
            g_signal_connect_data(mi, "activate", G_CALLBACK(cb_open_recent),
                                  g_strdup(fpath), (GClosureNotify)g_free, 0);
            gtk_menu_shell_append(GTK_MENU_SHELL(s_recent_menu), mi);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(s_recent_menu), gtk_separator_menu_item_new());
        GtkWidget *clr = gtk_menu_item_new_with_mnemonic(T("menu.recent.clear", "_Clear Recent Files"));
        g_signal_connect(clr, "activate", G_CALLBACK(cb_clear_recent), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(s_recent_menu), clr);
    }
    gtk_widget_show_all(s_recent_menu);
}

void main_recent_file_add(const char *path)
{
    /* Remove existing entry for this path */
    for (guint i = 0; i < s_recent_files->len; i++) {
        if (strcmp((char *)s_recent_files->pdata[i], path) == 0) {
            g_free(s_recent_files->pdata[i]);
            g_ptr_array_remove_index(s_recent_files, i);
            break;
        }
    }
    /* Prepend */
    g_ptr_array_insert(s_recent_files, 0, g_strdup(path));
    /* Trim */
    while (s_recent_files->len > MAX_RECENT) {
        g_free(s_recent_files->pdata[s_recent_files->len - 1]);
        g_ptr_array_remove_index(s_recent_files, s_recent_files->len - 1);
    }
    recent_save();
    recent_rebuild_menu();
}

static void cb_clear_recent(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    for (guint j = 0; j < s_recent_files->len; j++)
        g_free(s_recent_files->pdata[j]);
    g_ptr_array_set_size(s_recent_files, 0);
    recent_save();
    recent_rebuild_menu();
}

/* ------------------------------------------------------------------ */
/* Encoding menu                                                       */
/* ------------------------------------------------------------------ */

static GtkWidget  *s_enc_items[32]; /* radio items, one per npp_encodings[] entry */
static gboolean    s_enc_updating = FALSE;

void main_sync_encoding_menu(const char *enc)
{
    if (!enc) enc = "UTF-8";
    s_enc_updating = TRUE;
    for (int i = 0; i < npp_encoding_count && i < 32; i++) {
        if (strcmp(npp_encodings[i].display, enc) == 0) {
            gtk_check_menu_item_set_active(
                GTK_CHECK_MENU_ITEM(s_enc_items[i]), TRUE);
            break;
        }
    }
    s_enc_updating = FALSE;
    statusbar_set_encoding(enc);
}

static void cb_set_encoding(GtkMenuItem *item, gpointer data)
{
    if (s_enc_updating) return;
    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) return;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    const char *enc = (const char *)data;
    g_free(doc->encoding);
    doc->encoding = g_strdup(enc);
    statusbar_set_encoding(enc);
}

/* File */
static void cb_new(GtkMenuItem *i, gpointer d)       { (void)i;(void)d; editor_new_doc(); }
static void cb_open(GtkMenuItem *i, gpointer d)      { (void)i;(void)d; editor_open_dialog(); }
static void cb_reload(GtkMenuItem *i, gpointer d)    { (void)i;(void)d; editor_reload_current(); }
static void cb_save(GtkMenuItem *i, gpointer d)      { (void)i;(void)d; editor_save(); }
static void cb_save_as(GtkMenuItem *i, gpointer d)   { (void)i;(void)d; editor_save_as_dialog(); }
static void cb_save_all(GtkMenuItem *i, gpointer d)  { (void)i;(void)d; editor_save_all(); }
static void cb_close(GtkMenuItem *i, gpointer d)     { (void)i;(void)d; editor_close_page(-1); }
static void cb_close_all(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    /* Close all pages from the last down; each close may leave one "new 1" behind. */
    int count = editor_page_count();
    for (int k = count - 1; k >= 0; k--)
        if (!editor_close_page(k)) break;
}
static void cb_close_all_but(GtkMenuItem *i, gpointer d) { (void)i;(void)d; editor_close_all_but_current(); }

static void cb_load_session(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    session_restore();
}

static void cb_save_session(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    session_save();
}

static void cb_quit(GtkMenuItem *i, gpointer app)
{
    (void)i;
    session_save();
    editor_close_all_quit(G_APPLICATION(app));
}

/* Edit */
static void cb_undo(GtkMenuItem *i, gpointer d)    { (void)i;(void)d; editor_undo(); }
static void cb_redo(GtkMenuItem *i, gpointer d)    { (void)i;(void)d; editor_redo(); }
static void cb_cut(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    if (g_prefs.copy_line_no_selection &&
        editor_send(SCI_GETSELECTIONSTART, 0, 0) == editor_send(SCI_GETSELECTIONEND, 0, 0))
        editor_send(SCI_LINECUT, 0, 0);
    else
        editor_cut();
}
static void cb_copy(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    if (g_prefs.copy_line_no_selection &&
        editor_send(SCI_GETSELECTIONSTART, 0, 0) == editor_send(SCI_GETSELECTIONEND, 0, 0))
        editor_send(SCI_LINECOPY, 0, 0);
    else
        editor_copy();
}
static void cb_paste(GtkMenuItem *i, gpointer d)   { (void)i;(void)d; editor_paste(); }
static void cb_selall(GtkMenuItem *i, gpointer d)  { (void)i;(void)d; editor_select_all(); }

/* Search */
static GtkWidget *s_main_window = NULL;

static void cb_find(GtkMenuItem *i, gpointer d)
{
    (void)i;(void)d;
    findreplace_set_sci(editor_current_doc()->sci);
    findreplace_show(s_main_window, NULL, FALSE);
}

static void cb_replace(GtkMenuItem *i, gpointer d)
{
    (void)i;(void)d;
    findreplace_set_sci(editor_current_doc()->sci);
    findreplace_show(s_main_window, NULL, TRUE);
}

static void cb_find_in_files(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    const char *sel = NULL;
    /* Pre-fill with selection if short enough */
    if (doc) {
        sptr_t ss = editor_send(SCI_GETSELECTIONSTART, 0, 0);
        sptr_t se = editor_send(SCI_GETSELECTIONEND,   0, 0);
        if (se > ss && se - ss < 256) {
            static char selbuf[256];
            Sci_TextRangeFull tr;
            tr.chrg.cpMin  = (Sci_Position)ss;
            tr.chrg.cpMax  = (Sci_Position)se;
            tr.lpstrText   = selbuf;
            editor_send(SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
            sel = selbuf;
        }
    }
    findinfiles_show(s_main_window, sel);
}

static void cb_goto(GtkMenuItem *i, gpointer d)   { (void)i;(void)d; editor_goto_line_dialog(); }

static void cb_column_editor(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    columneditor_show(s_main_window);
}

/* Multi-select helpers */
static void ensure_word_selected(void)
{
    sptr_t ss = editor_send(SCI_GETSELECTIONSTART, 0, 0);
    sptr_t se = editor_send(SCI_GETSELECTIONEND,   0, 0);
    if (ss == se) {
        sptr_t pos    = editor_send(SCI_GETCURRENTPOS,     0, 0);
        sptr_t wstart = editor_send(SCI_WORDSTARTPOSITION, (uptr_t)pos, 1);
        sptr_t wend   = editor_send(SCI_WORDENDPOSITION,   (uptr_t)pos, 1);
        if (wend > wstart)
            editor_send(SCI_SETSEL, (uptr_t)wstart, (sptr_t)wend);
    }
}

static void cb_select_all_occurrences(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    ensure_word_selected();
    if (editor_send(SCI_GETSELECTIONSTART, 0, 0) ==
        editor_send(SCI_GETSELECTIONEND,   0, 0)) return;
    editor_send(SCI_MULTIPLESELECTADDEACH, 0, 0);
}

static void cb_add_next_occurrence(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    ensure_word_selected();
    if (editor_send(SCI_GETSELECTIONSTART, 0, 0) ==
        editor_send(SCI_GETSELECTIONEND,   0, 0)) return;
    editor_send(SCI_MULTIPLESELECTADDNEXT, 0, 0);
}

/* Edit — delete / indent / clipboard */
static void cb_delete(GtkMenuItem *i, gpointer d)        { (void)i;(void)d; editor_send(SCI_CLEAR, 0, 0); }
static void cb_indent(GtkMenuItem *i, gpointer d)        { (void)i;(void)d; editor_send(SCI_TAB, 0, 0); }
static void cb_unindent(GtkMenuItem *i, gpointer d)      { (void)i;(void)d; editor_send(SCI_BACKTAB, 0, 0); }

static void cb_copy_filepath(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc || !doc->filepath) return;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), doc->filepath, -1);
}

static void cb_copy_filename(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc || !doc->filepath) return;
    gchar *base = g_path_get_basename(doc->filepath);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), base, -1);
    g_free(base);
}

static void cb_copy_dirpath(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc || !doc->filepath) return;
    gchar *dir = g_path_get_dirname(doc->filepath);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), dir, -1);
    g_free(dir);
}

/* Search — find next/prev */
static void cb_find_next(GtkMenuItem *i, gpointer d) { (void)i;(void)d; findreplace_find_next(); }
static void cb_find_prev(GtkMenuItem *i, gpointer d) { (void)i;(void)d; findreplace_find_prev(); }

/* View — tab navigation */
static void cb_next_tab(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    GtkWidget *nb = editor_get_notebook();
    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb));
    int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(nb));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), (cur + 1) % n);
}
static void cb_prev_tab(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    GtkWidget *nb = editor_get_notebook();
    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb));
    int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(nb));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), (cur + n - 1) % n);
}
static void cb_first_tab(GtkMenuItem *i, gpointer d) { (void)i;(void)d; gtk_notebook_set_current_page(GTK_NOTEBOOK(editor_get_notebook()), 0); }
static void cb_last_tab(GtkMenuItem *i, gpointer d)  { (void)i;(void)d; gtk_notebook_set_current_page(GTK_NOTEBOOK(editor_get_notebook()), -1); }
static void cb_select_tab_n(GtkMenuItem *i, gpointer data)
{
    (void)i;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(editor_get_notebook()), GPOINTER_TO_INT(data) - 1);
}

/* View — zoom */
static void cb_zoom_in     (GtkMenuItem *i, gpointer d) { (void)i;(void)d; editor_send(SCI_ZOOMIN,  0, 0); }
static void cb_zoom_out    (GtkMenuItem *i, gpointer d) { (void)i;(void)d; editor_send(SCI_ZOOMOUT, 0, 0); }
static void cb_zoom_restore(GtkMenuItem *i, gpointer d) { (void)i;(void)d; editor_send(SCI_SETZOOM, 0, 0); }

/* View — always on top */
static gboolean s_always_on_top = FALSE;
static void cb_always_on_top(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    s_always_on_top = gtk_check_menu_item_get_active(item);
    gtk_window_set_keep_above(GTK_WINDOW(s_main_window), s_always_on_top);
}

/* View — fold current level */
static void cb_fold_current(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    Sci_Position line = (Sci_Position)editor_send(SCI_LINEFROMPOSITION,
        (uptr_t)editor_send(SCI_GETCURRENTPOS, 0, 0), 0);
    editor_send(SCI_FOLDLINE, (uptr_t)line, SC_FOLDACTION_CONTRACT);
}
static void cb_unfold_current(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    Sci_Position line = (Sci_Position)editor_send(SCI_LINEFROMPOSITION,
        (uptr_t)editor_send(SCI_GETCURRENTPOS, 0, 0), 0);
    editor_send(SCI_FOLDLINE, (uptr_t)line, SC_FOLDACTION_EXPAND);
}

/* Macro */
static void cb_macro_start(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    macro_start_recording(doc->sci);
    toolbar_update_macro_buttons();
}

static void cb_macro_stop(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    macro_stop_recording(doc->sci);
    toolbar_update_macro_buttons();
}

static void cb_macro_play(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    macro_playback(doc->sci);
}

static void cb_macro_play_n(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    macro_playback_n(doc->sci, GTK_WINDOW(s_main_window));
}

/* View — panels */
static void cb_toggle_doclist(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    doclist_set_visible(gtk_check_menu_item_get_active(item));
}

static void cb_toggle_workspace(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    workspace_set_visible(gtk_check_menu_item_get_active(item));
}

static void cb_toggle_funclist(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    funclist_set_visible(gtk_check_menu_item_get_active(item));
}

static void cb_open_folder_workspace(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Open Folder as Workspace",
        GTK_WINDOW(s_main_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT,
        NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        workspace_set_folder(folder);
        workspace_set_visible(TRUE);
        g_free(folder);
    }
    gtk_widget_destroy(dlg);
}

/* Settings */
static void cb_style_editor(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    styleeditor_show(s_main_window, editor_reapply_styles);
}

static void cb_shortcut_mapper(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    shortcut_mapper_show(s_main_window);
}

static void cb_preferences(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    prefs_dialog_show(s_main_window);
}

void main_doclist_refresh(void)
{
    doclist_refresh();
}

/* Called from prefs.c when show_full_path_in_title changes */
void main_refresh_title(void)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    const char *mod = doc->modified ? "*" : "";
    char buf[512];
    if (doc->filepath) {
        const char *name = g_prefs.show_full_path_in_title
                           ? doc->filepath
                           : g_path_get_basename(doc->filepath);
        snprintf(buf, sizeof(buf), "%s%s — Notepad++ Linux", mod, name);
    } else {
        snprintf(buf, sizeof(buf), "%snew %d — Notepad++ Linux", mod, doc->new_index);
    }
    gtk_window_set_title(GTK_WINDOW(s_main_window), buf);
}

/* Edge column state — declared early so line-op callbacks can read it */
static gboolean s_edge_enabled = FALSE;
static int      s_edge_column  = 80;

/* Word wrap menu item — kept as a pointer so on_switch_page can sync it */
static GtkWidget *s_mi_wrap = NULL;

/* ------------------------------------------------------------------ */
/* Show/hide symbols                                                  */
/* ------------------------------------------------------------------ */

static gboolean s_show_whitespace = FALSE;
static gboolean s_show_eol_marks  = FALSE;
static gboolean s_show_linenums   = TRUE;
static gboolean s_show_fold       = TRUE;
static gboolean s_show_bookmarks  = FALSE;

/* Apply current symbol visibility state to a single Scintilla widget. */
static void apply_view_symbols(GtkWidget *sci)
{
    if (!sci) return;
    scintilla_send_message(SCINTILLA(sci), SCI_SETVIEWWS,
        s_show_whitespace ? SC_WS_VISIBLEALWAYS : SC_WS_INVISIBLE, 0);
    scintilla_send_message(SCINTILLA(sci), SCI_SETVIEWEOL,
        s_show_eol_marks, 0);

    /* Compute line-number margin width from actual font metrics */
    if (s_show_linenums) {
        int n = (int)scintilla_send_message(SCINTILLA(sci), SCI_GETLINECOUNT, 0, 0);
        /* Ensure room for at least 4 digits; "_" provides side padding */
        char buf[24];
        snprintf(buf, sizeof(buf), "_%d_", n < 9999 ? 9999 : n);
        int w = (int)scintilla_send_message(SCINTILLA(sci), SCI_TEXTWIDTH,
                                            STYLE_LINENUMBER, (sptr_t)buf);
        if (w < 32) w = 32; /* fallback before widget is realized */
        scintilla_send_message(SCINTILLA(sci), SCI_SETMARGINWIDTHN, 0, w);
    } else {
        scintilla_send_message(SCINTILLA(sci), SCI_SETMARGINWIDTHN, 0, 0);
    }

    scintilla_send_message(SCINTILLA(sci), SCI_SETMARGINWIDTHN,
        2, s_show_fold ? 16 : 0);
    scintilla_send_message(SCINTILLA(sci), SCI_SETMARGINWIDTHN,
        1, s_show_bookmarks ? 16 : 0);
}

/* Called from editor.c after setup_sci so new tabs get correct widths */
void main_apply_view_symbols(GtkWidget *sci)
{
    apply_view_symbols(sci);
}

/* Apply to every open tab. */
static void apply_view_symbols_all(void)
{
    int n = editor_page_count();
    for (int i = 0; i < n; i++) {
        NppDoc *doc = editor_doc_at(i);
        if (doc) apply_view_symbols(doc->sci);
    }
}

static void cb_toggle_whitespace(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    s_show_whitespace = gtk_check_menu_item_get_active(item);
    apply_view_symbols_all();
}

static void cb_toggle_eol_marks(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    s_show_eol_marks = gtk_check_menu_item_get_active(item);
    apply_view_symbols_all();
}

static void cb_toggle_linenums(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    s_show_linenums = gtk_check_menu_item_get_active(item);
    apply_view_symbols_all();
}

static void cb_toggle_fold(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    s_show_fold = gtk_check_menu_item_get_active(item);
    apply_view_symbols_all();
}

static void cb_toggle_bookmarks(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    s_show_bookmarks = gtk_check_menu_item_get_active(item);
    apply_view_symbols_all();
}

/* ------------------------------------------------------------------ */
/* Fold controls                                                       */
/* ------------------------------------------------------------------ */

static void cb_fold_all(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    editor_send(SCI_FOLDALL, SC_FOLDACTION_CONTRACT, 0);
}

static void cb_unfold_all(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    editor_send(SCI_FOLDALL, SC_FOLDACTION_EXPAND, 0);
}

static void cb_fold_level(GtkMenuItem *i, gpointer d)
{
    (void)i;
    int level = GPOINTER_TO_INT(d);
    int n = (int)editor_send(SCI_GETLINECOUNT, 0, 0);
    for (int ln = 0; ln < n; ln++) {
        int lvl = (int)editor_send(SCI_GETFOLDLEVEL, (uptr_t)ln, 0);
        if ((lvl & SC_FOLDLEVELHEADERFLAG) &&
            (lvl & SC_FOLDLEVELNUMBERMASK) == SC_FOLDLEVELBASE + (level - 1))
            editor_send(SCI_FOLDLINE, (uptr_t)ln, SC_FOLDACTION_CONTRACT);
    }
}

static void cb_unfold_level(GtkMenuItem *i, gpointer d)
{
    (void)i;
    int level = GPOINTER_TO_INT(d);
    int n = (int)editor_send(SCI_GETLINECOUNT, 0, 0);
    for (int ln = 0; ln < n; ln++) {
        int lvl = (int)editor_send(SCI_GETFOLDLEVEL, (uptr_t)ln, 0);
        if ((lvl & SC_FOLDLEVELHEADERFLAG) &&
            (lvl & SC_FOLDLEVELNUMBERMASK) == SC_FOLDLEVELBASE + (level - 1))
            editor_send(SCI_FOLDLINE, (uptr_t)ln, SC_FOLDACTION_EXPAND);
    }
}

/* ------------------------------------------------------------------ */
/* Comment / Uncomment                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *lang;
    const char *line_start;
    const char *block_start;
    const char *block_end;
} CommentStyle;

static const CommentStyle kCommentStyles[] = {
    /* C-family / curly-brace */
    {"c",           "//",   "/*",   "*/"},
    {"cpp",         "//",   "/*",   "*/"},
    {"objc",        "//",   "/*",   "*/"},
    {"cs",          "//",   "/*",   "*/"},
    {"java",        "//",   "/*",   "*/"},
    {"javascript",  "//",   "/*",   "*/"},
    {"typescript",  "//",   "/*",   "*/"},
    {"swift",       "//",   "/*",   "*/"},
    {"rc",          "//",   "/*",   "*/"},
    {"actionscript","//",   "/*",   "*/"},
    {"go",          "//",   "/*",   "*/"},
    {"rust",        "//",   "/*",   "*/"},
    {"d",           "//",   "/*",   "*/"},
    {"verilog",     "//",   "/*",   "*/"},
    {"oscript",     "//",   "/*",   "*/"},
    {"baanc",       "//",   "/*",   "*/"},
    {"escript",     "//",   "/*",   "*/"},
    /* Web */
    {"html",        NULL,   "<!--", "-->"},
    {"asp",         NULL,   "<!--", "-->"},
    {"xml",         NULL,   "<!--", "-->"},
    {"css",         NULL,   "/*",   "*/"},
    {"php",         "//",   "/*",   "*/"},
    /* SQL / dash */
    {"sql",         "--",   "/*",   "*/"},
    {"mssql",       "--",   "/*",   "*/"},
    {"lua",         "--",   "--[[", "]]"},
    {"haskell",     "--",   "{-",   "-}"},
    {"ada",         "--",   NULL,   NULL},
    {"vhdl",        "--",   NULL,   NULL},
    /* Hash */
    {"python",      "#",    NULL,   NULL},
    {"ruby",        "#",    NULL,   NULL},
    {"perl",        "#",    NULL,   NULL},
    {"bash",        "#",    NULL,   NULL},
    {"makefile",    "#",    NULL,   NULL},
    {"tcl",         "#",    NULL,   NULL},
    {"r",           "#",    NULL,   NULL},
    {"raku",        "#",    NULL,   NULL},
    {"coffeescript","#",    "###",  "###"},
    {"yaml",        "#",    NULL,   NULL},
    {"toml",        "#",    NULL,   NULL},
    {"cmake",       "#",    NULL,   NULL},
    {"nim",         "#",    NULL,   NULL},
    {"gdscript",    "#",    NULL,   NULL},
    {"avs",         "#",    NULL,   NULL},
    /* Percent */
    {"latex",       "%",    NULL,   NULL},
    {"tex",         "%",    NULL,   NULL},
    {"erlang",      "%",    NULL,   NULL},
    {"postscript",  "%",    NULL,   NULL},
    {"matlab",      "%",    "%{",   "%}"},
    {"visualprolog","%",    "/*",   "*/"},
    /* Powershell */
    {"powershell",  "#",    "<#",   "#>"},
    /* Semicolon */
    {"ini",         ";",    NULL,   NULL},
    {"props",       ";",    NULL,   NULL},
    {"registry",    ";",    NULL,   NULL},
    {"asm",         ";",    NULL,   NULL},
    {"lisp",        ";",    "#|",   "|#"},
    {"scheme",      ";",    "#|",   "|#"},
    {"nsis",        ";",    "/*",   "*/"},
    {"inno",        ";",    NULL,   NULL},
    {"autoit",      ";",    "#cs",  "#ce"},
    {"kix",         ";",    NULL,   NULL},
    {"nncrontab",   ";",    NULL,   NULL},
    {"csound",      ";",    "/*",   "*/"},
    {"hollywood",   ";",    "/*",   "*/"},
    {"purebasic",   ";",    NULL,   NULL},
    /* VB / BASIC apostrophe */
    {"vb",          "'",    NULL,   NULL},
    {"freebasic",   "'",    NULL,   NULL},
    {"blitzbasic",  "'",    NULL,   NULL},
    /* Batch */
    {"batch",       "REM ", NULL,   NULL},
    /* Fortran */
    {"fortran",     "!",    NULL,   NULL},
    {"fortran77",   "C ",   NULL,   NULL},
    /* Pascal / ML */
    {"pascal",      NULL,   "{",    "}"},
    {"caml",        NULL,   "(*",   "*)"},
    /* Other */
    {"sas",         "*",    "/*",   "*/"},
    {"cobol",       "*",    NULL,   NULL},
    {"spice",       "*",    NULL,   NULL},
    {"forth",       "\\ ",  NULL,   NULL},
    {NULL, NULL, NULL, NULL}
};

static const CommentStyle *comment_style_for_doc(NppDoc *doc)
{
    const char *lang = (const char *)g_object_get_data(G_OBJECT(doc->sci), "npp-lang");
    if (!lang || !*lang) return NULL;
    for (const CommentStyle *cs = kCommentStyles; cs->lang; cs++)
        if (strcmp(cs->lang, lang) == 0) return cs;
    return NULL;
}

/* Toggle single-line comment on all lines covered by the selection.
   If all non-empty lines are already commented → remove prefix;
   otherwise → add prefix. */
static void toggle_line_comment(NppDoc *doc, const CommentStyle *cs)
{
    if (!cs || !cs->line_start) return;
    const char *pfx    = cs->line_start;
    gsize        pfxlen = strlen(pfx);

    sptr_t sel_start = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
    sptr_t sel_end   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);

    int line_first = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_start, 0);
    int line_last  = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_end,   0);
    /* Don't include a line whose first position is sel_end (cursor at col 0) */
    if (sel_start != sel_end && line_last > line_first) {
        sptr_t ll_start = scintilla_send_message(SCINTILLA(doc->sci), SCI_POSITIONFROMLINE, line_last, 0);
        if (sel_end <= ll_start) line_last--;
    }

    sptr_t rstart = scintilla_send_message(SCINTILLA(doc->sci), SCI_POSITIONFROMLINE, line_first, 0);
    sptr_t rend   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLINEENDPOSITION, line_last,  0);
    sptr_t len    = rend - rstart;
    if (len <= 0) return;

    char *buf = g_malloc(len + 2);
    Sci_TextRangeFull tr = { { rstart, rend }, buf };
    scintilla_send_message(SCINTILLA(doc->sci), SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);

    /* Determine whether to add or remove */
    gboolean all_commented = TRUE;
    for (char *p = buf; *p; ) {
        char *nl = strpbrk(p, "\r\n");
        size_t llen = nl ? (size_t)(nl - p) : strlen(p);
        /* skip empty lines for the "all commented" check */
        gboolean empty = TRUE;
        for (size_t k = 0; k < llen; k++) if (p[k] != ' ' && p[k] != '\t') { empty = FALSE; break; }
        if (!empty && (llen < pfxlen || strncmp(p, pfx, pfxlen) != 0)) {
            all_commented = FALSE;
            break;
        }
        if (nl) { if (*nl == '\r' && *(nl+1) == '\n') nl++; p = nl + 1; } else break;
    }

    GString *out = g_string_sized_new(len + 64);
    for (char *p = buf; *p; ) {
        char *nl   = strpbrk(p, "\r\n");
        size_t llen = nl ? (size_t)(nl - p) : strlen(p);

        if (all_commented) {
            /* Remove prefix if present */
            if (llen >= pfxlen && strncmp(p, pfx, pfxlen) == 0) {
                p    += pfxlen;
                llen -= pfxlen;
                /* strip one optional space after prefix */
                if (llen > 0 && p[0] == ' ' && pfx[pfxlen-1] != ' ') { p++; llen--; }
            }
            g_string_append_len(out, p, llen);
        } else {
            g_string_append(out, pfx);
            g_string_append_len(out, p, llen);
        }

        if (nl) {
            if (*nl == '\r' && *(nl+1) == '\n') { g_string_append_len(out, "\r\n", 2); p = nl + 2; }
            else                                 { g_string_append_c(out, *nl);         p = nl + 1; }
        } else { break; }
    }
    g_free(buf);

    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETTARGETRANGE, (uptr_t)rstart, (sptr_t)rend);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    g_string_free(out, TRUE);
}

/* Toggle block comment around the exact selection.
   If the selection already starts/ends with the delimiters → remove them;
   otherwise → wrap. */
static void toggle_block_comment(NppDoc *doc, const CommentStyle *cs)
{
    if (!cs || !cs->block_start || !cs->block_end) return;
    const char *bs  = cs->block_start;
    const char *be  = cs->block_end;
    gsize        bsl = strlen(bs);
    gsize        bel = strlen(be);

    sptr_t sel_start = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
    sptr_t sel_end   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);
    /* Require an actual selection */
    if (sel_start == sel_end) return;

    sptr_t len = sel_end - sel_start;
    char *buf  = g_malloc(len + 2);
    Sci_TextRangeFull tr = { { sel_start, sel_end }, buf };
    scintilla_send_message(SCINTILLA(doc->sci), SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);

    gboolean already = ((gsize)len >= bsl + bel
                        && strncmp(buf, bs, bsl) == 0
                        && strncmp(buf + len - bel, be, bel) == 0);

    GString *out = g_string_sized_new(len + bsl + bel + 4);
    if (already) {
        g_string_append_len(out, buf + bsl, len - bsl - bel);
    } else {
        g_string_append(out, bs);
        g_string_append_len(out, buf, len);
        g_string_append(out, be);
    }
    g_free(buf);

    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETTARGETRANGE, (uptr_t)sel_start, (sptr_t)sel_end);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    g_string_free(out, TRUE);
}

static void cb_toggle_line_comment(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    toggle_line_comment(doc, comment_style_for_doc(doc));
}

static void cb_toggle_block_comment(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    toggle_block_comment(doc, comment_style_for_doc(doc));
}

/* ------------------------------------------------------------------ */
/* Selection helpers (used by case, trim, base64, hex, hash)         */
/* ------------------------------------------------------------------ */

/* Returns a g_malloc'd buffer with the current selection text and its byte
   length via *out_len. Returns NULL (silently) when nothing is selected. */
static char *get_selection(NppDoc *doc, gsize *out_len)
{
    sptr_t s = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
    sptr_t e = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);
    if (s == e) return NULL;
    gsize len = (gsize)(e - s);
    char *buf = g_malloc(len + 1);
    Sci_TextRangeFull tr = { { s, e }, buf };
    scintilla_send_message(SCINTILLA(doc->sci), SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
    *out_len = len;
    return buf;
}

static void replace_selection(NppDoc *doc, const char *text)
{
    scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACESEL, 0, (sptr_t)text);
}

/* ------------------------------------------------------------------ */
/* Case conversion                                                    */
/* ------------------------------------------------------------------ */

static void cb_case_upper   (GtkMenuItem *i, gpointer d) { (void)i;(void)d; editor_send(SCI_UPPERCASE, 0, 0); }
static void cb_case_lower   (GtkMenuItem *i, gpointer d) { (void)i;(void)d; editor_send(SCI_LOWERCASE, 0, 0); }

static void case_transform(gboolean(*fn)(char *, gsize))
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    gsize len;
    char *buf = get_selection(doc, &len);
    if (!buf) return;
    if (fn(buf, len)) replace_selection(doc, buf);
    g_free(buf);
}

static gboolean do_proper(char *buf, gsize len)
{
    gboolean cap = TRUE;
    for (gsize k = 0; k < len; k++) {
        if (g_ascii_isalpha(buf[k])) {
            buf[k] = cap ? g_ascii_toupper(buf[k]) : g_ascii_tolower(buf[k]);
            cap = FALSE;
        } else if (buf[k] == ' ' || buf[k] == '\t' || buf[k] == '\n' || buf[k] == '\r') {
            cap = TRUE;
        }
    }
    return TRUE;
}

static gboolean do_sentence(char *buf, gsize len)
{
    gboolean cap = TRUE;
    for (gsize k = 0; k < len; k++) {
        if (g_ascii_isalpha(buf[k])) {
            buf[k] = cap ? g_ascii_toupper(buf[k]) : g_ascii_tolower(buf[k]);
            cap = FALSE;
        } else if (buf[k] == '.' || buf[k] == '!' || buf[k] == '?') {
            cap = TRUE;
        }
    }
    return TRUE;
}

static gboolean do_invert(char *buf, gsize len)
{
    for (gsize k = 0; k < len; k++) {
        if      (g_ascii_isupper(buf[k])) buf[k] = g_ascii_tolower(buf[k]);
        else if (g_ascii_islower(buf[k])) buf[k] = g_ascii_toupper(buf[k]);
    }
    return TRUE;
}

static gboolean do_random(char *buf, gsize len)
{
    for (gsize k = 0; k < len; k++)
        if (g_ascii_isalpha(buf[k]))
            buf[k] = g_random_boolean() ? g_ascii_toupper(buf[k]) : g_ascii_tolower(buf[k]);
    return TRUE;
}

static void cb_case_proper  (GtkMenuItem *i, gpointer d) { (void)i;(void)d; case_transform(do_proper);   }
static void cb_case_sentence(GtkMenuItem *i, gpointer d) { (void)i;(void)d; case_transform(do_sentence); }
static void cb_case_invert  (GtkMenuItem *i, gpointer d) { (void)i;(void)d; case_transform(do_invert);   }
static void cb_case_random  (GtkMenuItem *i, gpointer d) { (void)i;(void)d; case_transform(do_random);   }

/* ------------------------------------------------------------------ */
/* Line operations                                                    */
/* ------------------------------------------------------------------ */

static void cb_line_duplicate(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    editor_send(SCI_LINEDUPLICATE, 0, 0);
}

static void cb_line_delete(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    editor_send(SCI_LINEDELETE, 0, 0);
}

static void cb_line_move_up(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    editor_send(SCI_MOVESELECTEDLINESUP, 0, 0);
}

static void cb_line_move_down(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    editor_send(SCI_MOVESELECTEDLINESDOWN, 0, 0);
}

static void cb_join_lines(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    sptr_t sel_start = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
    sptr_t sel_end   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);

    sptr_t rstart, rend;
    if (sel_start == sel_end) {
        int line  = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_start, 0);
        int total = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLINECOUNT, 0, 0);
        if (line + 1 >= total) return;
        rstart = scintilla_send_message(SCINTILLA(doc->sci), SCI_POSITIONFROMLINE, line, 0);
        rend   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLINEENDPOSITION, line + 1, 0);
    } else {
        int ls = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_start, 0);
        int le = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_end,   0);
        rstart = scintilla_send_message(SCINTILLA(doc->sci), SCI_POSITIONFROMLINE, ls, 0);
        rend   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLINEENDPOSITION, le, 0);
    }

    sptr_t len = rend - rstart;
    if (len <= 0) return;

    char *buf = g_malloc(len + 2);
    Sci_TextRangeFull tr = { { rstart, rend }, buf };
    scintilla_send_message(SCINTILLA(doc->sci), SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);

    GString *out = g_string_sized_new(len);
    for (char *p = buf; *p; ) {
        if (*p == '\r' || *p == '\n') {
            while (out->len && out->str[out->len - 1] == ' ')
                g_string_truncate(out, out->len - 1);
            if (*p == '\r' && *(p + 1) == '\n') p++;
            p++;
            while (*p == ' ' || *p == '\t') p++;
            if (*p) g_string_append_c(out, ' ');
        } else {
            g_string_append_c(out, *p++);
        }
    }
    g_free(buf);

    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETTARGETRANGE, (uptr_t)rstart, (sptr_t)rend);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    g_string_free(out, TRUE);
}

static void cb_split_lines(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    int col = s_edge_column > 0 ? s_edge_column : 80;
    int eol_mode = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_GETEOLMODE, 0, 0);
    const char *eol = eol_mode == SC_EOL_CRLF ? "\r\n" : eol_mode == SC_EOL_CR ? "\r" : "\n";

    sptr_t sel_start = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
    sptr_t sel_end   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);

    sptr_t rstart, rend;
    if (sel_start == sel_end) {
        int line = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_start, 0);
        rstart = scintilla_send_message(SCINTILLA(doc->sci), SCI_POSITIONFROMLINE, line, 0);
        rend   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLINEENDPOSITION, line, 0);
    } else {
        int ls = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_start, 0);
        int le = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_end,   0);
        rstart = scintilla_send_message(SCINTILLA(doc->sci), SCI_POSITIONFROMLINE, ls, 0);
        rend   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLINEENDPOSITION, le, 0);
    }

    sptr_t len = rend - rstart;
    if (len <= 0) return;

    char *buf = g_malloc(len + 2);
    Sci_TextRangeFull tr = { { rstart, rend }, buf };
    scintilla_send_message(SCINTILLA(doc->sci), SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);

    size_t eol_len = strlen(eol);
    GString *out = g_string_sized_new(len + 16);
    char *p = buf;
    while (*p) {
        char *nl = strpbrk(p, "\r\n");
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
        size_t pos = 0;
        while (pos + (size_t)col < line_len) {
            int brk = -1;
            for (int j = col; j >= 0; j--) {
                if (p[pos + j] == ' ') { brk = j; break; }
            }
            if (brk < 0) brk = col;
            g_string_append_len(out, p + pos, brk);
            g_string_append_len(out, eol, eol_len);
            pos += brk;
            if (p[pos] == ' ') pos++;
        }
        g_string_append_len(out, p + pos, line_len - pos);
        if (nl) {
            if (*nl == '\r' && *(nl + 1) == '\n') nl++;
            p = nl + 1;
            g_string_append_len(out, eol, eol_len);
        } else {
            break;
        }
    }
    g_free(buf);

    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETTARGETRANGE, (uptr_t)rstart, (sptr_t)rend);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    g_string_free(out, TRUE);
}

static void cb_line_insert_above(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    editor_send(SCI_HOME,    0, 0);
    editor_send(SCI_NEWLINE, 0, 0);
    editor_send(SCI_LINEUP,  0, 0);
}

static void cb_line_insert_below(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    editor_send(SCI_LINEEND, 0, 0);
    editor_send(SCI_NEWLINE, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Remove duplicate / blank lines                                     */
/* ------------------------------------------------------------------ */

/* Fetch every line of the document as a NUL-terminated string (no EOL). */
static GPtrArray *collect_lines(ScintillaObject *sci, int *out_eol_mode)
{
    int nlines  = (int)scintilla_send_message(sci, SCI_GETLINECOUNT, 0, 0);
    int eol     = (int)scintilla_send_message(sci, SCI_GETEOLMODE,   0, 0);
    if (out_eol_mode) *out_eol_mode = eol;

    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    for (int ln = 0; ln < nlines; ln++) {
        Sci_Position ls  = scintilla_send_message(sci, SCI_POSITIONFROMLINE,   (uptr_t)ln, 0);
        Sci_Position le  = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, (uptr_t)ln, 0);
        int content_len  = (int)(le - ls);
        char *buf = g_malloc(content_len + 1);
        if (content_len > 0) {
            Sci_TextRangeFull tr = { { ls, le }, buf };
            scintilla_send_message(sci, SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
        }
        buf[content_len] = '\0';
        g_ptr_array_add(arr, buf);
    }
    return arr;
}

static void replace_doc_with_lines(ScintillaObject *sci, GPtrArray *lines, int eol_mode)
{
    const char *eol_str = (eol_mode == SC_EOL_CRLF) ? "\r\n"
                        : (eol_mode == SC_EOL_CR)   ? "\r"
                        :                             "\n";
    GString *out = g_string_new(NULL);
    for (guint i = 0; i < lines->len; i++) {
        if (i > 0) g_string_append(out, eol_str);
        g_string_append(out, (char *)lines->pdata[i]);
    }
    Sci_Position doc_end = scintilla_send_message(sci, SCI_GETLENGTH, 0, 0);
    scintilla_send_message(sci, SCI_SETTARGETRANGE, 0, (sptr_t)doc_end);
    scintilla_send_message(sci, SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    g_string_free(out, TRUE);
}

static void cb_remove_duplicate_lines(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    ScintillaObject *sci = SCINTILLA(doc->sci);
    int eol_mode;
    GPtrArray *src = collect_lines(sci, &eol_mode);

    GPtrArray *out  = g_ptr_array_new();
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint n = 0; n < src->len; n++) {
        const char *line = src->pdata[n];
        if (!g_hash_table_contains(seen, line)) {
            g_hash_table_add(seen, (gpointer)line);
            g_ptr_array_add(out, (gpointer)line);
        }
    }
    replace_doc_with_lines(sci, out, eol_mode);
    g_hash_table_destroy(seen);
    g_ptr_array_free(out, FALSE);
    g_ptr_array_free(src, TRUE);
}

static void cb_remove_blank_lines(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    ScintillaObject *sci = SCINTILLA(doc->sci);
    int eol_mode;
    GPtrArray *src = collect_lines(sci, &eol_mode);

    GPtrArray *out = g_ptr_array_new();
    for (guint n = 0; n < src->len; n++) {
        const char *line = src->pdata[n];
        /* keep line if it has at least one non-whitespace character */
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0')
            g_ptr_array_add(out, (gpointer)line);
    }
    replace_doc_with_lines(sci, out, eol_mode);
    g_ptr_array_free(out, FALSE);
    g_ptr_array_free(src, TRUE);
}

/* ------------------------------------------------------------------ */
/* Sort lines                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    SORT_LEXIC,
    SORT_LEXIC_CI,
    SORT_LENGTH,
    SORT_NUMERIC,
    SORT_RANDOM,
    SORT_REVERSE
} SortMode;

static int cmp_lexic   (gconstpointer a, gconstpointer b) {
    return g_strcmp0(*(const char **)a, *(const char **)b);
}
static int cmp_lexic_ci(gconstpointer a, gconstpointer b) {
    return g_ascii_strcasecmp(*(const char **)a, *(const char **)b);
}
static int cmp_length  (gconstpointer a, gconstpointer b) {
    int la = (int)strlen(*(const char **)a);
    int lb = (int)strlen(*(const char **)b);
    return (la > lb) - (la < lb);
}
static int cmp_numeric (gconstpointer a, gconstpointer b) {
    double da = g_ascii_strtod(*(const char **)a, NULL);
    double db = g_ascii_strtod(*(const char **)b, NULL);
    return (da > db) - (da < db);
}
static int cmp_random  (gconstpointer a, gconstpointer b) {
    (void)a; (void)b;
    return (g_random_boolean() ? 1 : -1);
}

static void do_sort(SortMode mode)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    ScintillaObject *sci = SCINTILLA(doc->sci);
    int eol_mode;
    GPtrArray *lines = collect_lines(sci, &eol_mode);

    if (mode == SORT_REVERSE) {
        /* reverse in place */
        guint lo = 0, hi = lines->len - 1;
        while (lo < hi) {
            gpointer tmp   = lines->pdata[lo];
            lines->pdata[lo] = lines->pdata[hi];
            lines->pdata[hi] = tmp;
            lo++; hi--;
        }
    } else {
        GCompareFunc fn;
        switch (mode) {
            case SORT_LEXIC_CI: fn = cmp_lexic_ci; break;
            case SORT_LENGTH:   fn = cmp_length;   break;
            case SORT_NUMERIC:  fn = cmp_numeric;  break;
            case SORT_RANDOM:   fn = cmp_random;   break;
            default:            fn = cmp_lexic;    break;
        }
        g_ptr_array_sort(lines, fn);
    }

    replace_doc_with_lines(sci, lines, eol_mode);
    g_ptr_array_free(lines, TRUE);
}

static void cb_sort_lexic   (GtkMenuItem *i, gpointer d) { (void)i;(void)d; do_sort(SORT_LEXIC);    }
static void cb_sort_lexic_ci(GtkMenuItem *i, gpointer d) { (void)i;(void)d; do_sort(SORT_LEXIC_CI); }
static void cb_sort_length  (GtkMenuItem *i, gpointer d) { (void)i;(void)d; do_sort(SORT_LENGTH);   }
static void cb_sort_numeric (GtkMenuItem *i, gpointer d) { (void)i;(void)d; do_sort(SORT_NUMERIC);  }
static void cb_sort_random  (GtkMenuItem *i, gpointer d) { (void)i;(void)d; do_sort(SORT_RANDOM);   }
static void cb_sort_reverse (GtkMenuItem *i, gpointer d) { (void)i;(void)d; do_sort(SORT_REVERSE);  }

/* ------------------------------------------------------------------ */
/* Trim whitespace                                                    */
/* ------------------------------------------------------------------ */

typedef enum { TRIM_TRAILING, TRIM_LEADING, TRIM_BOTH } TrimMode;

static void do_trim(TrimMode mode)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    sptr_t sel_start = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
    sptr_t sel_end   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);

    sptr_t rstart, rend;
    if (sel_start == sel_end) {
        rstart = 0;
        rend   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLENGTH, 0, 0);
    } else {
        int ls = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_start, 0);
        int le = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, sel_end,   0);
        rstart = scintilla_send_message(SCINTILLA(doc->sci), SCI_POSITIONFROMLINE, ls, 0);
        rend   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLINEENDPOSITION, le, 0);
    }

    sptr_t len = rend - rstart;
    if (len <= 0) return;

    char *buf = g_malloc(len + 2);
    Sci_TextRangeFull tr = { { rstart, rend }, buf };
    scintilla_send_message(SCINTILLA(doc->sci), SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);

    GString *out = g_string_sized_new(len);
    char *p = buf;
    while (*p) {
        char *nl = strpbrk(p, "\r\n");
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);

        size_t ts = 0, te = line_len;
        if (mode == TRIM_LEADING || mode == TRIM_BOTH)
            while (ts < te && (p[ts] == ' ' || p[ts] == '\t')) ts++;
        if (mode == TRIM_TRAILING || mode == TRIM_BOTH)
            while (te > ts && (p[te - 1] == ' ' || p[te - 1] == '\t')) te--;

        g_string_append_len(out, p + ts, te - ts);

        if (nl) {
            if (*nl == '\r' && *(nl + 1) == '\n') { g_string_append_len(out, "\r\n", 2); p = nl + 2; }
            else                                   { g_string_append_c(out, *nl);         p = nl + 1; }
        } else {
            break;
        }
    }
    g_free(buf);

    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETTARGETRANGE, (uptr_t)rstart, (sptr_t)rend);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    g_string_free(out, TRUE);
}

static void cb_trim_trailing(GtkMenuItem *i, gpointer d) { (void)i; (void)d; do_trim(TRIM_TRAILING); }
static void cb_trim_leading (GtkMenuItem *i, gpointer d) { (void)i; (void)d; do_trim(TRIM_LEADING);  }
static void cb_trim_both    (GtkMenuItem *i, gpointer d) { (void)i; (void)d; do_trim(TRIM_BOTH);     }

/* ------------------------------------------------------------------ */
/* Whitespace conversions — spaces↔tabs                               */
/* ------------------------------------------------------------------ */

/* Replace leading spaces on every line with tabs (or vice versa).
   tab_width: number of spaces that equal one tab (read from Scintilla). */
static void do_spaces_to_tabs(NppDoc *doc)
{
    ScintillaObject *sci = SCINTILLA(doc->sci);
    int tab_w = (int)scintilla_send_message(sci, SCI_GETTABWIDTH, 0, 0);
    if (tab_w < 1) tab_w = 4;

    int nlines = (int)scintilla_send_message(sci, SCI_GETLINECOUNT, 0, 0);
    GString *out = g_string_new(NULL);

    for (int ln = 0; ln < nlines; ln++) {
        Sci_Position ls = scintilla_send_message(sci, SCI_POSITIONFROMLINE, (uptr_t)ln, 0);
        Sci_Position le = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, (uptr_t)ln, 0);
        if (le <= ls) {
            /* empty line — preserve existing EOL by appending nothing */
            if (ln < nlines - 1) {
                /* get the line including EOL */
                int raw_len = (int)(scintilla_send_message(sci, SCI_LINELENGTH, (uptr_t)ln, 0));
                char *raw = g_malloc(raw_len + 1);
                Sci_TextRangeFull tr = { { ls, ls + raw_len }, raw };
                scintilla_send_message(sci, SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
                raw[raw_len] = '\0';
                /* keep only the EOL part (after le) */
                int eol_len = (int)(ls + raw_len - le);
                g_string_append_len(out, raw + (le - ls), eol_len);
                g_free(raw);
            }
            continue;
        }

        /* fetch full line (including EOL) */
        int raw_len = (int)scintilla_send_message(sci, SCI_LINELENGTH, (uptr_t)ln, 0);
        char *raw = g_malloc(raw_len + 1);
        Sci_TextRangeFull tr = { { ls, ls + raw_len }, raw };
        scintilla_send_message(sci, SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
        raw[raw_len] = '\0';

        /* count leading spaces */
        int content_len = (int)(le - ls);
        int sp = 0;
        while (sp < content_len && raw[sp] == ' ') sp++;

        int tabs   = sp / tab_w;
        int remain = sp % tab_w;

        for (int t = 0; t < tabs;   t++) g_string_append_c(out, '\t');
        for (int s = 0; s < remain; s++) g_string_append_c(out, ' ');
        /* rest of line content + EOL */
        g_string_append_len(out, raw + sp, raw_len - sp);
        g_free(raw);
    }

    Sci_Position doc_end = scintilla_send_message(sci, SCI_GETLENGTH, 0, 0);
    scintilla_send_message(sci, SCI_SETTARGETRANGE, 0, (sptr_t)doc_end);
    scintilla_send_message(sci, SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    g_string_free(out, TRUE);
}

static void do_tabs_to_spaces(NppDoc *doc)
{
    ScintillaObject *sci = SCINTILLA(doc->sci);
    int tab_w = (int)scintilla_send_message(sci, SCI_GETTABWIDTH, 0, 0);
    if (tab_w < 1) tab_w = 4;

    int nlines = (int)scintilla_send_message(sci, SCI_GETLINECOUNT, 0, 0);
    GString *out = g_string_new(NULL);

    for (int ln = 0; ln < nlines; ln++) {
        Sci_Position ls = scintilla_send_message(sci, SCI_POSITIONFROMLINE, (uptr_t)ln, 0);
        int raw_len = (int)scintilla_send_message(sci, SCI_LINELENGTH, (uptr_t)ln, 0);
        if (raw_len <= 0) continue;

        char *raw = g_malloc(raw_len + 1);
        Sci_TextRangeFull tr = { { ls, ls + raw_len }, raw };
        scintilla_send_message(sci, SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
        raw[raw_len] = '\0';

        Sci_Position le = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, (uptr_t)ln, 0);
        int content_len = (int)(le - ls);

        /* expand leading tabs only */
        int col = 0;
        int i   = 0;
        while (i < content_len && (raw[i] == '\t' || raw[i] == ' ')) {
            if (raw[i] == '\t') {
                int spaces = tab_w - (col % tab_w);
                for (int s = 0; s < spaces; s++) g_string_append_c(out, ' ');
                col += spaces;
            } else {
                g_string_append_c(out, ' ');
                col++;
            }
            i++;
        }
        /* rest of line + EOL */
        g_string_append_len(out, raw + i, raw_len - i);
        g_free(raw);
    }

    Sci_Position doc_end = scintilla_send_message(sci, SCI_GETLENGTH, 0, 0);
    scintilla_send_message(sci, SCI_SETTARGETRANGE, 0, (sptr_t)doc_end);
    scintilla_send_message(sci, SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    g_string_free(out, TRUE);
}

static void cb_spaces_to_tabs(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (doc) do_spaces_to_tabs(doc);
}

static void cb_tabs_to_spaces(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (doc) do_tabs_to_spaces(doc);
}

/* ------------------------------------------------------------------ */
/* Insert date/time                                                   */
/* ------------------------------------------------------------------ */

static void insert_datetime(const char *fmt)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    GDateTime *dt = g_date_time_new_now_local();
    gchar *str = g_date_time_format(dt, fmt);
    g_date_time_unref(dt);
    if (str) {
        scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACESEL, 0, (sptr_t)str);
        g_free(str);
    }
}

static void cb_insert_date_short(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    insert_datetime("%H:%M:%S %m/%d/%Y");
}

static void cb_insert_date_long(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    insert_datetime("%A, %B %d, %Y %H:%M:%S");
}

/* ------------------------------------------------------------------ */
/* Base64 / Hex tools                                                 */
/* ------------------------------------------------------------------ */

static void cb_base64_encode(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    gsize len;
    char *buf = get_selection(doc, &len);
    if (!buf) return;
    gchar *enc = g_base64_encode((const guchar *)buf, len);
    g_free(buf);
    replace_selection(doc, enc);
    g_free(enc);
}

static void cb_base64_decode(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    gsize len;
    char *buf = get_selection(doc, &len);
    if (!buf) return;
    gsize out_len = 0;
    guchar *dec = g_base64_decode(buf, &out_len);
    g_free(buf);
    if (!dec) return;
    /* dec may contain null bytes — write as raw bytes via SCI_REPLACESEL
       which treats lParam as null-terminated; for binary safety use target */
    sptr_t s = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
    sptr_t e = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETTARGETRANGE, (uptr_t)s, (sptr_t)e);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACETARGET, (uptr_t)out_len, (sptr_t)dec);
    g_free(dec);
}

static void cb_ascii_to_hex(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    gsize len;
    char *buf = get_selection(doc, &len);
    if (!buf) return;
    GString *hex = g_string_sized_new(len * 2);
    for (gsize k = 0; k < len; k++)
        g_string_append_printf(hex, "%02x", (unsigned char)buf[k]);
    g_free(buf);
    replace_selection(doc, hex->str);
    g_string_free(hex, TRUE);
}

static void cb_hex_to_ascii(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    gsize len;
    char *buf = get_selection(doc, &len);
    if (!buf) return;
    GString *out = g_string_sized_new(len / 2 + 1);
    gboolean ok = TRUE;
    for (gsize k = 0; k < len; ) {
        while (k < len && (buf[k] == ' ' || buf[k] == '\t' || buf[k] == '\n' || buf[k] == '\r')) k++;
        if (k >= len) break;
        if (k + 1 >= len || !g_ascii_isxdigit(buf[k]) || !g_ascii_isxdigit(buf[k + 1])) {
            ok = FALSE; break;
        }
        char byte = (char)((g_ascii_xdigit_value(buf[k]) << 4) | g_ascii_xdigit_value(buf[k + 1]));
        g_string_append_c(out, byte);
        k += 2;
    }
    g_free(buf);
    if (ok) {
        sptr_t s = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
        sptr_t e = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);
        scintilla_send_message(SCINTILLA(doc->sci), SCI_SETTARGETRANGE, (uptr_t)s, (sptr_t)e);
        scintilla_send_message(SCINTILLA(doc->sci), SCI_REPLACETARGET, (uptr_t)out->len, (sptr_t)out->str);
    }
    g_string_free(out, TRUE);
}

/* ------------------------------------------------------------------ */
/* Hash tools                                                         */
/* ------------------------------------------------------------------ */

static void cb_hash_generator(GtkMenuItem *item, gpointer d)
{
    (void)item; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    sptr_t sel_start = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONSTART, 0, 0);
    sptr_t sel_end   = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETSELECTIONEND,   0, 0);
    gboolean has_sel = sel_start != sel_end;

    sptr_t rstart = has_sel ? sel_start : 0;
    sptr_t rend   = has_sel ? sel_end
                            : scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLENGTH, 0, 0);
    sptr_t len = rend - rstart;
    if (len <= 0) return;

    char *buf = g_malloc(len + 1);
    Sci_TextRangeFull tr = { { rstart, rend }, buf };
    scintilla_send_message(SCINTILLA(doc->sci), SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);

    static const struct { GChecksumType type; const char *name; } algos[] = {
        { G_CHECKSUM_MD5,    "MD5"     },
        { G_CHECKSUM_SHA1,   "SHA-1"   },
        { G_CHECKSUM_SHA256, "SHA-256" },
        { G_CHECKSUM_SHA512, "SHA-512" },
    };

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        TM("dlg.hash.title", "Hash Generator"),
        GTK_WINDOW(s_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        TM("dlg.Find.2", "_Close"), GTK_RESPONSE_CLOSE,
        NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_widget_set_margin_top(grid, 8);
    gtk_widget_set_margin_bottom(grid, 8);

    /* Source info label */
    char info[64];
    snprintf(info, sizeof(info),
             has_sel ? "Selection (%ld bytes)" : "Document (%ld bytes)", (long)len);
    GtkWidget *info_lbl = gtk_label_new(info);
    gtk_widget_set_halign(info_lbl, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), info_lbl, 0, 0, 2, 1);

    for (int i = 0; i < 4; i++) {
        gchar *hash = g_compute_checksum_for_data(algos[i].type,
                                                  (const guchar *)buf, (gsize)len);
        GtkWidget *lbl = gtk_label_new(algos[i].name);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);

        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), hash);
        gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
        gtk_entry_set_width_chars(GTK_ENTRY(entry), 64);

        gtk_grid_attach(GTK_GRID(grid), lbl,   0, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, i + 1, 1, 1);
        g_free(hash);
    }
    g_free(buf);

    GtkWidget *ca = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_pack_start(GTK_BOX(ca), grid, FALSE, FALSE, 0);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* Edge column                                                        */
/* ------------------------------------------------------------------ */

static void apply_edge(GtkWidget *sci)
{
    if (!sci) return;
    scintilla_send_message(SCINTILLA(sci), SCI_SETEDGEMODE,
        s_edge_enabled ? SC_EDGE_LINE : SC_EDGE_NONE, 0);
    scintilla_send_message(SCINTILLA(sci), SCI_SETEDGECOLUMN,
        (uptr_t)s_edge_column, 0);
}

static void apply_edge_all(void)
{
    int n = editor_page_count();
    for (int i = 0; i < n; i++) {
        NppDoc *doc = editor_doc_at(i);
        if (doc) apply_edge(doc->sci);
    }
}

static void cb_toggle_edge(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    s_edge_enabled = gtk_check_menu_item_get_active(item);
    apply_edge_all();
}

static void cb_set_edge_column(GtkMenuItem *item, gpointer d)
{
    (void)item; (void)d;
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        TM("dlg.edgecol.title", "Set Edge Column"),
        GTK_WINDOW(s_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        TM("dlg.Find.2", "_Close"),  GTK_RESPONSE_CANCEL,
        TM("cmd.41006",  "_OK"),     GTK_RESPONSE_ACCEPT,
        NULL);

    GtkWidget *spin = gtk_spin_button_new_with_range(1, 512, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), s_edge_column);

    GtkWidget *lbl = gtk_label_new(TM("dlg.edgecol.label", "Column:"));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox, 12);
    gtk_widget_set_margin_top(hbox, 8);
    gtk_widget_set_margin_bottom(hbox, 8);
    gtk_box_pack_start(GTK_BOX(hbox), lbl,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 0);

    GtkWidget *ca = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_pack_start(GTK_BOX(ca), hbox, FALSE, FALSE, 0);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        s_edge_column = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
        if (s_edge_enabled) apply_edge_all();
    }
    gtk_widget_destroy(dlg);
}

/* ------------------------------------------------------------------ */
/* Word wrap (per-tab)                                                */
/* ------------------------------------------------------------------ */

static void cb_toggle_word_wrap(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    gboolean on = gtk_check_menu_item_get_active(item);
    doc->word_wrap = on;
    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETWRAPMODE,
                           on ? SC_WRAP_WORD : SC_WRAP_NONE, 0);
    toolbar_sync_toggles(doc->sci);
}

static void wrap_menu_sync(gboolean on)
{
    if (!s_mi_wrap) return;
    g_signal_handlers_block_by_func(s_mi_wrap, G_CALLBACK(cb_toggle_word_wrap), NULL);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s_mi_wrap), on);
    g_signal_handlers_unblock_by_func(s_mi_wrap, G_CALLBACK(cb_toggle_word_wrap), NULL);
}

/* ------------------------------------------------------------------ */
/* Mark styles (5 color highlight indicators)                         */
/* ------------------------------------------------------------------ */

#define MARK_STYLE_COUNT 5

/* Colors shown in menu labels — kept in sync with editor.c setup_sci */
static const char *kMarkStyleNames[MARK_STYLE_COUNT] = {
    "Mark Style 1 (Yellow)",
    "Mark Style 2 (Cyan)",
    "Mark Style 3 (Blue)",
    "Mark Style 4 (Orange)",
    "Mark Style 5 (Magenta)",
};

static void mark_apply(int style)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    ScintillaObject *sci = SCINTILLA(doc->sci);

    Sci_Position sel_start = scintilla_send_message(sci, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position sel_end   = scintilla_send_message(sci, SCI_GETSELECTIONEND,   0, 0);
    if (sel_start == sel_end) return;   /* nothing selected */

    scintilla_send_message(sci, SCI_SETINDICATORCURRENT, (uptr_t)style, 0);
    scintilla_send_message(sci, SCI_INDICATORFILLRANGE,
                           (uptr_t)sel_start, (sptr_t)(sel_end - sel_start));
}

static void mark_clear(int style)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    ScintillaObject *sci = SCINTILLA(doc->sci);

    Sci_Position doc_len = scintilla_send_message(sci, SCI_GETLENGTH, 0, 0);
    scintilla_send_message(sci, SCI_SETINDICATORCURRENT, (uptr_t)style, 0);
    scintilla_send_message(sci, SCI_INDICATORCLEARRANGE, 0, (sptr_t)doc_len);
}

static void mark_clear_all_styles(void)
{
    for (int s = 0; s < MARK_STYLE_COUNT; s++)
        mark_clear(s);
}

static void mark_jump(int style, gboolean forward)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    ScintillaObject *sci = SCINTILLA(doc->sci);

    Sci_Position pos     = scintilla_send_message(sci, SCI_GETCURRENTPOS, 0, 0);
    Sci_Position doc_len = scintilla_send_message(sci, SCI_GETLENGTH,     0, 0);

    scintilla_send_message(sci, SCI_SETINDICATORCURRENT, (uptr_t)style, 0);

    Sci_Position found = -1;
    if (forward) {
        /* search from pos+1 to end, then wrap */
        Sci_Position start = scintilla_send_message(sci, SCI_INDICATOREND,
                                                    (uptr_t)style, (sptr_t)pos);
        if (start < doc_len)
            found = start;
        else {
            start = scintilla_send_message(sci, SCI_INDICATOREND,
                                           (uptr_t)style, 0);
            if (start < doc_len && start > 0)
                found = 0; /* there is an indicator somewhere from the start */
        }
        /* re-search from the beginning if wrapped */
        if (found == 0) {
            Sci_Position s2 = scintilla_send_message(sci, SCI_INDICATOREND,
                                                     (uptr_t)style, 0);
            found = (s2 > 0) ? 0 : -1;
        }
    } else {
        Sci_Position end = scintilla_send_message(sci, SCI_INDICATORSTART,
                                                  (uptr_t)style, (sptr_t)pos);
        if (end > 0)
            found = scintilla_send_message(sci, SCI_INDICATORSTART,
                                           (uptr_t)style, (sptr_t)(end - 1));
        if (found < 0) {
            /* wrap: search from end of doc backwards */
            Sci_Position s2 = scintilla_send_message(sci, SCI_INDICATORSTART,
                                                     (uptr_t)style, (sptr_t)(doc_len - 1));
            if (s2 >= 0) found = s2;
        }
    }
    if (found >= 0)
        scintilla_send_message(sci, SCI_GOTOPOS, (uptr_t)found, 0);
}

/* --- Callbacks: one per style × action --- */
static void cb_mark1(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_apply(0);}
static void cb_mark2(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_apply(1);}
static void cb_mark3(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_apply(2);}
static void cb_mark4(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_apply(3);}
static void cb_mark5(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_apply(4);}

static void cb_mark_clear1(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_clear(0);}
static void cb_mark_clear2(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_clear(1);}
static void cb_mark_clear3(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_clear(2);}
static void cb_mark_clear4(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_clear(3);}
static void cb_mark_clear5(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_clear(4);}

static void cb_mark_clear_all(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_clear_all_styles();}

static void cb_mark_next1(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(0,TRUE);}
static void cb_mark_next2(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(1,TRUE);}
static void cb_mark_next3(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(2,TRUE);}
static void cb_mark_next4(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(3,TRUE);}
static void cb_mark_next5(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(4,TRUE);}

static void cb_mark_prev1(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(0,FALSE);}
static void cb_mark_prev2(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(1,FALSE);}
static void cb_mark_prev3(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(2,FALSE);}
static void cb_mark_prev4(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(3,FALSE);}
static void cb_mark_prev5(GtkMenuItem *i,gpointer d){(void)i;(void)d; mark_jump(4,FALSE);}

/* ------------------------------------------------------------------ */
/* Go to matching brace                                               */
/* ------------------------------------------------------------------ */

static void cb_goto_matching_brace(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    GtkWidget *sci = doc->sci;

    Sci_Position pos = (Sci_Position)editor_send(SCI_GETCURRENTPOS, 0, 0);
    static const char braces[] = "()[]{}<>";
    Sci_Position brace_pos = -1;
    char ch = (char)scintilla_send_message(SCINTILLA(sci), SCI_GETCHARAT, (uptr_t)pos, 0);
    if (strchr(braces, ch))
        brace_pos = pos;
    else if (pos > 0) {
        ch = (char)scintilla_send_message(SCINTILLA(sci), SCI_GETCHARAT, (uptr_t)(pos - 1), 0);
        if (strchr(braces, ch))
            brace_pos = pos - 1;
    }
    if (brace_pos < 0) return;

    Sci_Position match = (Sci_Position)scintilla_send_message(
        SCINTILLA(sci), SCI_BRACEMATCH, (uptr_t)brace_pos, 0);
    if (match >= 0)
        scintilla_send_message(SCINTILLA(sci), SCI_GOTOPOS, (uptr_t)match, 0);
}

/* ------------------------------------------------------------------ */
/* Bookmarks                                                          */
/* ------------------------------------------------------------------ */

#define BOOKMARK_MASK  (1 << SC_MARKNUM_BOOKMARK)

void main_toggle_bookmark_at_line(GtkWidget *sci, int line)
{
    int markers = (int)scintilla_send_message(SCINTILLA(sci),
                                              SCI_MARKERGET, (uptr_t)line, 0);
    if (markers & BOOKMARK_MASK)
        scintilla_send_message(SCINTILLA(sci), SCI_MARKERDELETE,
                               (uptr_t)line, SC_MARKNUM_BOOKMARK);
    else
        scintilla_send_message(SCINTILLA(sci), SCI_MARKERADD,
                               (uptr_t)line, SC_MARKNUM_BOOKMARK);
}

static void cb_bookmark_toggle(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    sptr_t pos  = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETCURRENTPOS, 0, 0);
    int line = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, (uptr_t)pos, 0);
    main_toggle_bookmark_at_line(doc->sci, line);
}

static void cb_bookmark_next(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    sptr_t pos  = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETCURRENTPOS, 0, 0);
    int line = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, (uptr_t)pos, 0);
    /* search from next line; wrap around if not found */
    int found = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_MARKERNEXT,
                                            (uptr_t)(line + 1), BOOKMARK_MASK);
    if (found < 0)
        found = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_MARKERNEXT,
                                            0, BOOKMARK_MASK);
    if (found >= 0)
        scintilla_send_message(SCINTILLA(doc->sci), SCI_GOTOLINE, (uptr_t)found, 0);
}

static void cb_bookmark_prev(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    sptr_t pos  = scintilla_send_message(SCINTILLA(doc->sci), SCI_GETCURRENTPOS, 0, 0);
    int line = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_LINEFROMPOSITION, (uptr_t)pos, 0);
    int found = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_MARKERPREV,
                                            (uptr_t)(line - 1), BOOKMARK_MASK);
    if (found < 0) {
        int nlines = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_GETLINECOUNT, 0, 0);
        found = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_MARKERPREV,
                                            (uptr_t)(nlines - 1), BOOKMARK_MASK);
    }
    if (found >= 0)
        scintilla_send_message(SCINTILLA(doc->sci), SCI_GOTOLINE, (uptr_t)found, 0);
}

static void cb_bookmark_clear_all(GtkMenuItem *i, gpointer d)
{
    (void)i; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    scintilla_send_message(SCINTILLA(doc->sci), SCI_MARKERDELETEALL,
                           SC_MARKNUM_BOOKMARK, 0);
}

/* Collect all bookmarked lines then perform an action on them */
typedef enum { BM_CUT, BM_COPY, BM_DELETE } BmAction;

static void bookmarked_lines_action(BmAction action)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    ScintillaObject *sci = SCINTILLA(doc->sci);

    int eol_mode;
    GPtrArray *all = collect_lines(sci, &eol_mode);
    const char *eol_str = (eol_mode == SC_EOL_CRLF) ? "\r\n"
                        : (eol_mode == SC_EOL_CR)   ? "\r" : "\n";

    GPtrArray *bookmarked = g_ptr_array_new();
    GPtrArray *remaining  = g_ptr_array_new();

    for (guint ln = 0; ln < all->len; ln++) {
        int markers = (int)scintilla_send_message(sci, SCI_MARKERGET, ln, 0);
        if (markers & BOOKMARK_MASK)
            g_ptr_array_add(bookmarked, all->pdata[ln]);
        else
            g_ptr_array_add(remaining, all->pdata[ln]);
    }

    if (action == BM_COPY || action == BM_CUT) {
        /* Copy bookmarked lines to clipboard */
        GString *clip = g_string_new(NULL);
        for (guint i = 0; i < bookmarked->len; i++) {
            if (i > 0) g_string_append(clip, eol_str);
            g_string_append(clip, (char *)bookmarked->pdata[i]);
        }
        GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(cb, clip->str, (gint)clip->len);
        g_string_free(clip, TRUE);
    }

    if (action == BM_CUT || action == BM_DELETE) {
        replace_doc_with_lines(sci, remaining, eol_mode);
        /* Clear any leftover bookmark markers (they were on deleted lines) */
        scintilla_send_message(sci, SCI_MARKERDELETEALL, SC_MARKNUM_BOOKMARK, 0);
    }

    g_ptr_array_free(bookmarked, FALSE);
    g_ptr_array_free(remaining,  FALSE);
    g_ptr_array_free(all, TRUE);
}

static void cb_bookmark_cut   (GtkMenuItem *i, gpointer d) { (void)i;(void)d; bookmarked_lines_action(BM_CUT);    }
static void cb_bookmark_copy  (GtkMenuItem *i, gpointer d) { (void)i;(void)d; bookmarked_lines_action(BM_COPY);   }
static void cb_bookmark_delete(GtkMenuItem *i, gpointer d) { (void)i;(void)d; bookmarked_lines_action(BM_DELETE); }

/* ------------------------------------------------------------------ */
/* EOL menu                                                           */
/* ------------------------------------------------------------------ */

/* Indexed by SC_EOL_CRLF=0, SC_EOL_CR=1, SC_EOL_LF=2 */
static GtkWidget *s_eol_items[3];

static void cb_eol_toggled(GtkCheckMenuItem *item, gpointer data)
{
    if (!gtk_check_menu_item_get_active(item)) return;
    int mode = GPOINTER_TO_INT(data);
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETEOLMODE, (uptr_t)mode, 0);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_CONVERTEOLS, (uptr_t)mode, 0);
    statusbar_update_from_sci(doc->sci);
}

static void eol_menu_sync(int mode)
{
    if (mode < 0 || mode > 2) mode = SC_EOL_LF;
    GtkCheckMenuItem *item = GTK_CHECK_MENU_ITEM(s_eol_items[mode]);
    if (!item) return;
    g_signal_handlers_block_by_func(item, G_CALLBACK(cb_eol_toggled),
                                    GINT_TO_POINTER(mode));
    gtk_check_menu_item_set_active(item, TRUE);
    g_signal_handlers_unblock_by_func(item, G_CALLBACK(cb_eol_toggled),
                                      GINT_TO_POINTER(mode));
}

/* ------------------------------------------------------------------ */
/* Language menu                                                       */
/* ------------------------------------------------------------------ */

/* Maps lang key → GtkRadioMenuItem* for checkmark syncing. */
static GHashTable *s_lang_item_map = NULL;

/* Display names for menu labels (lang key → human label). */
typedef struct { const char *lang; const char *label; } LangLabel;
static const LangLabel kLangLabels[] = {
    /* C-family */
    {"c",           "C"},
    {"cpp",         "C++"},
    {"objc",        "Objective-C"},
    {"cs",          "C#"},
    {"java",        "Java"},
    {"javascript",  "JavaScript"},
    {"typescript",  "TypeScript"},
    {"swift",       "Swift"},
    {"rc",          "Resource file"},
    {"actionscript","ActionScript"},
    {"go",          "Go"},
    /* Web */
    {"html",        "HTML"},
    {"asp",         "ASP"},
    {"xml",         "XML"},
    {"css",         "CSS"},
    {"json",        "JSON"},
    {"php",         "PHP"},
    /* Scripting */
    {"python",      "Python"},
    {"ruby",        "Ruby"},
    {"perl",        "Perl"},
    {"lua",         "Lua"},
    {"bash",        "Shell"},
    {"powershell",  "PowerShell"},
    {"batch",       "Batch"},
    {"tcl",         "TCL"},
    {"r",           "R"},
    {"raku",        "Raku"},
    {"coffeescript","CoffeeScript"},
    /* Systems */
    {"rust",        "Rust"},
    {"d",           "D"},
    /* Markup / Config */
    {"markdown",    "Markdown"},
    {"latex",       "LaTeX"},
    {"tex",         "TeX"},
    {"yaml",        "YAML"},
    {"toml",        "TOML"},
    {"ini",         "INI"},
    {"props",       "Properties"},
    {"makefile",    "Makefile"},
    {"cmake",       "CMake"},
    {"diff",        "Diff"},
    {"registry",    "Registry"},
    {"nsis",        "NSIS"},
    {"inno",        "Inno Setup"},
    /* Database */
    {"sql",         "SQL"},
    {"mssql",       "MS-SQL"},
    /* Scientific */
    {"fortran",     "Fortran (free)"},
    {"fortran77",   "Fortran (fixed)"},
    {"pascal",      "Pascal"},
    {"haskell",     "Haskell"},
    {"caml",        "CAML"},
    {"lisp",        "Lisp"},
    {"scheme",      "Scheme"},
    {"erlang",      "Erlang"},
    {"nim",         "Nim"},
    {"gdscript",    "GDScript"},
    {"sas",         "SAS"},
    /* Hardware */
    {"vhdl",        "VHDL"},
    {"verilog",     "Verilog"},
    {"asm",         "Assembly"},
    /* Other */
    {"ada",         "Ada"},
    {"cobol",       "COBOL"},
    {"vb",          "Visual Basic"},
    {"autoit",      "AutoIt"},
    {"postscript",  "PostScript"},
    {"matlab",      "MATLAB"},
    {"smalltalk",   "Smalltalk"},
    {"forth",       "Forth"},
    {"oscript",     "OScript"},
    {"avs",         "AVS"},
    {"hollywood",   "Hollywood"},
    {"purebasic",   "PureBasic"},
    {"freebasic",   "FreeBasic"},
    {"blitzbasic",  "BlitzBasic"},
    {"kix",         "KiXtart"},
    {"visualprolog","Visual Prolog"},
    {"baanc",       "BaanC"},
    {"nncrontab",   "NNCronTab"},
    {"csound",      "CSound"},
    {"escript",     "EScript"},
    {"spice",       "Spice"},
    {NULL, NULL}
};

static const char *lang_label(const char *lang)
{
    for (const LangLabel *l = kLangLabels; l->lang; l++)
        if (strcmp(l->lang, lang) == 0) return l->label;
    return lang;
}

static void cb_lang_toggled(GtkCheckMenuItem *item, gpointer data)
{
    if (!gtk_check_menu_item_get_active(item)) return;
    const char *lang = (const char *)data;   /* "" = Normal Text */
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    lexer_apply(doc->sci, lang[0] ? lang : NULL);
    statusbar_set_language(lang[0] ? lexer_display_name(lang) : NULL);
}

/* Update the checked radio item to match the current tab's language. */
static void lang_menu_sync(const char *lang)
{
    if (!s_lang_item_map) return;
    const char *key = (lang && lang[0]) ? lang : "";
    GtkWidget *widget = g_hash_table_lookup(s_lang_item_map, key);
    if (!widget)   /* unknown language: fall back to Normal Text */
        widget = g_hash_table_lookup(s_lang_item_map, "");
    if (!widget) return;
    GtkCheckMenuItem *item = GTK_CHECK_MENU_ITEM(widget);
    /* Retrieve exact data pointer used when the signal was connected */
    const char *stored_key = g_object_get_data(G_OBJECT(item), "npp-lang-key");
    if (!stored_key) stored_key = key;
    g_signal_handlers_block_by_func(item, G_CALLBACK(cb_lang_toggled), (gpointer)stored_key);
    gtk_check_menu_item_set_active(item, TRUE);
    g_signal_handlers_unblock_by_func(item, G_CALLBACK(cb_lang_toggled), (gpointer)stored_key);
}

/* Add one radio item to a menu, register it in the map, advance the group. */
static void add_lang_item(GtkWidget *menu, GSList **group,
                          const char *lang_key, const char *label)
{
    GtkWidget *item = gtk_radio_menu_item_new_with_label(*group, label);
    *group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "toggled", G_CALLBACK(cb_lang_toggled), (gpointer)lang_key);
    g_hash_table_insert(s_lang_item_map, (gpointer)lang_key, item);
    /* Store the exact pointer used as signal data for block/unblock in lang_menu_sync */
    g_object_set_data(G_OBJECT(item), "npp-lang-key", (gpointer)lang_key);
}

/* Add a labelled submenu of language items to the Language menu. */
static void add_lang_group(GtkWidget *lang_menu, GSList **group,
                           const char *group_label,
                           const char * const *langs, int n)
{
    GtkWidget *sub_item = gtk_menu_item_new_with_label(group_label);
    GtkWidget *sub_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(sub_item), sub_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(lang_menu), sub_item);
    for (int i = 0; i < n; i++)
        add_lang_item(sub_menu, group, langs[i], lang_label(langs[i]));
}

static GtkWidget *build_language_menu(GtkWidget *bar)
{
    udl_load_all();
    s_lang_item_map = g_hash_table_new(g_str_hash, g_str_equal);
    GSList *group = NULL;

    GtkWidget *top_item = gtk_menu_item_new_with_mnemonic(T("menu.language", "_Language"));
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top_item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), top_item);

    /* Normal Text at the top */
    add_lang_item(menu, &group, "", "Normal Text");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* Language groups */
    static const char * const c_family[] = {
        "c","cpp","objc","cs","java","javascript","typescript",
        "swift","rc","actionscript","go"
    };
    static const char * const web[] = { "html","asp","xml","css","json","php" };
    static const char * const scripting[] = {
        "python","ruby","perl","lua","bash","powershell",
        "batch","tcl","r","raku","coffeescript"
    };
    static const char * const systems[] = { "rust","d" };
    static const char * const markup[] = {
        "markdown","latex","tex","yaml","toml","ini","props",
        "makefile","cmake","diff","registry","nsis","inno"
    };
    static const char * const database[] = { "sql","mssql" };
    static const char * const scientific[] = {
        "fortran","fortran77","pascal","haskell","caml","lisp",
        "scheme","erlang","nim","gdscript","sas"
    };
    static const char * const hardware[] = { "vhdl","verilog","asm" };
    static const char * const other[] = {
        "ada","cobol","vb","autoit","postscript","matlab","smalltalk",
        "forth","oscript","avs","hollywood","purebasic","freebasic",
        "blitzbasic","kix","visualprolog","baanc","nncrontab",
        "csound","escript","spice"
    };

#define NELEM(a) (int)(sizeof(a)/sizeof(a[0]))
    add_lang_group(menu, &group, "C, C++, C#, Java",  c_family,  NELEM(c_family));
    add_lang_group(menu, &group, "Web",                web,       NELEM(web));
    add_lang_group(menu, &group, "Scripting",          scripting, NELEM(scripting));
    add_lang_group(menu, &group, "Systems",            systems,   NELEM(systems));
    add_lang_group(menu, &group, "Markup / Config",    markup,    NELEM(markup));
    add_lang_group(menu, &group, "Database",           database,  NELEM(database));
    add_lang_group(menu, &group, "Scientific",         scientific,NELEM(scientific));
    add_lang_group(menu, &group, "Hardware",           hardware,  NELEM(hardware));
    add_lang_group(menu, &group, "Other",              other,     NELEM(other));
#undef NELEM

    /* User Defined Languages */
    int udl_n = udl_count();
    if (udl_n > 0) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        GtkWidget *udl_item = gtk_menu_item_new_with_label("User Defined Languages");
        GtkWidget *udl_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(udl_item), udl_menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), udl_item);
        for (int i = 0; i < udl_n; i++)
            add_lang_item(udl_menu, &group, udl_key(i), udl_name(i));
    }

    return menu;
}

/* ------------------------------------------------------------------ */
/* Menu builder helpers                                               */
/* ------------------------------------------------------------------ */

static GtkWidget *menu_item(const char *label, GCallback cb, gpointer data,
                             GtkAccelGroup *accel, guint key, GdkModifierType mod)
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    if (cb)
        g_signal_connect(item, "activate", cb, data);
    if (key && accel)
        gtk_widget_add_accelerator(item, "activate", accel, key, mod,
                                   GTK_ACCEL_VISIBLE);
    return item;
}

static GtkWidget *sep_item(void)
{
    return gtk_separator_menu_item_new();
}

/* Disabled placeholder for not-yet-implemented menu items */
static GtkWidget *nyi_item(const char *label)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    gtk_widget_set_sensitive(item, FALSE);
    return item;
}

static GtkWidget *submenu(GtkWidget *bar, const char *label)
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    return menu;
}

#define APPEND(menu, item) gtk_menu_shell_append(GTK_MENU_SHELL(menu), item)

/* Shortcut-mapper-aware menu item: uses loaded key override if present. */
static GtkWidget *smi(const char *id, const char *label,
                       GCallback cb, gpointer data,
                       GtkAccelGroup *accel,
                       guint dkey, GdkModifierType dmod)
{
    ShortcutEntry *e   = shortcut_find(id);
    guint           key = (e && e->current_key) ? e->current_key : dkey;
    GdkModifierType mod = (e && e->current_key) ? e->current_mod : dmod;
    GtkWidget *item = menu_item(label, cb, data, accel, key, mod);
    shortcut_register(id, item, accel);
    return item;
}

static GtkWidget *build_menubar(GtkWindow *window, GApplication *app)
{
    GtkAccelGroup *accel = gtk_accel_group_new();
    gtk_window_add_accel_group(window, accel);

    GtkWidget *bar = gtk_menu_bar_new();

    /* ---- File ---- */
    GtkWidget *file = submenu(bar, TM("menu.file", "_File"));
    APPEND(file, smi("cmd.new",  TM("cmd.41001", "_New"),   G_CALLBACK(cb_new),  NULL, accel, GDK_KEY_n, GDK_CONTROL_MASK));
    APPEND(file, smi("cmd.open",TM("cmd.41002", "_Open…"), G_CALLBACK(cb_open), NULL, accel, GDK_KEY_o, GDK_CONTROL_MASK));
    /* Recent Files submenu */
    {
        s_recent_item = gtk_menu_item_new_with_mnemonic(T("menu.recent", "Open _Recent"));
        s_recent_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(s_recent_item), s_recent_menu);
        APPEND(file, s_recent_item);
        recent_rebuild_menu();
    }
    /* Open Containing Folder submenu */
    {
        GtkWidget *ocf_item = gtk_menu_item_new_with_label("Open Containing Folder");
        GtkWidget *ocf_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(ocf_item), ocf_menu);
        APPEND(ocf_menu, nyi_item("Terminal"));
        APPEND(ocf_menu, nyi_item("File Manager"));
        APPEND(file, ocf_item);
    }
    APPEND(file, nyi_item("Open in Default Viewer"));
    APPEND(file, menu_item("Open Folder as Workspace…", G_CALLBACK(cb_open_folder_workspace), NULL, NULL, 0, 0));
    APPEND(file, sep_item());
    APPEND(file, menu_item(TM("cmd.reload", "Reload from Disk"), G_CALLBACK(cb_reload), NULL, NULL, 0, 0));
    APPEND(file, sep_item());
    APPEND(file, smi("cmd.save",  TM("cmd.41006", "_Save"),      G_CALLBACK(cb_save),   NULL, accel, GDK_KEY_s, GDK_CONTROL_MASK));
    APPEND(file, smi("cmd.saveas",TM("cmd.41008", "Save _As…"),  G_CALLBACK(cb_save_as),NULL, accel, GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    APPEND(file, nyi_item("Save a Copy As…"));
    APPEND(file, smi("cmd.saveall", TM("cmd.saveall", "Save All"), G_CALLBACK(cb_save_all), NULL, accel, GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK));
    APPEND(file, nyi_item("Rename…"));
    APPEND(file, sep_item());
    APPEND(file, smi("cmd.close", TM("cmd.41003", "_Close"),     G_CALLBACK(cb_close),  NULL, accel, GDK_KEY_w, GDK_CONTROL_MASK));
    APPEND(file, menu_item("Close All", G_CALLBACK(cb_close_all), NULL, NULL, 0, 0));
    /* Close Multiple Documents submenu */
    {
        GtkWidget *cm_item = gtk_menu_item_new_with_label("Close Multiple Documents");
        GtkWidget *cm_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(cm_item), cm_menu);
        APPEND(cm_menu, menu_item("Close All But Current",  G_CALLBACK(cb_close_all_but), NULL, NULL, 0, 0));
        APPEND(cm_menu, nyi_item("Close All to the Left"));
        APPEND(cm_menu, nyi_item("Close All to the Right"));
        APPEND(cm_menu, nyi_item("Close All Unchanged"));
        APPEND(file, cm_item);
    }
    APPEND(file, nyi_item("Move to Trash"));
    APPEND(file, sep_item());
    APPEND(file, menu_item("Load Session…", G_CALLBACK(cb_load_session), NULL, NULL, 0, 0));
    APPEND(file, menu_item("Save Session…", G_CALLBACK(cb_save_session), NULL, NULL, 0, 0));
    APPEND(file, sep_item());
    APPEND(file, nyi_item("Print…"));
    APPEND(file, nyi_item("Print Now"));
    APPEND(file, sep_item());
    APPEND(file, smi("cmd.quit",  TM("cmd.41011", "_Quit"),      G_CALLBACK(cb_quit),   app,  accel, GDK_KEY_q, GDK_CONTROL_MASK));

    /* ---- Edit ---- */
    GtkWidget *edit = submenu(bar, TM("menu.edit", "_Edit"));
    APPEND(edit, smi("cmd.undo",  TM("cmd.42003", "_Undo"),       G_CALLBACK(cb_undo),   NULL, accel, GDK_KEY_z, GDK_CONTROL_MASK));
    APPEND(edit, smi("cmd.redo",  TM("cmd.42004", "_Redo"),       G_CALLBACK(cb_redo),   NULL, accel, GDK_KEY_z, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    APPEND(edit, sep_item());
    APPEND(edit, smi("cmd.cut",   TM("cmd.42001", "Cu_t"),        G_CALLBACK(cb_cut),    NULL, accel, GDK_KEY_x, GDK_CONTROL_MASK));
    APPEND(edit, smi("cmd.copy",  TM("cmd.42002", "_Copy"),       G_CALLBACK(cb_copy),   NULL, accel, GDK_KEY_c, GDK_CONTROL_MASK));
    APPEND(edit, smi("cmd.paste", TM("cmd.42005", "_Paste"),      G_CALLBACK(cb_paste),  NULL, accel, GDK_KEY_v, GDK_CONTROL_MASK));
    APPEND(edit, menu_item(TM("cmd.delete", "_Delete"),           G_CALLBACK(cb_delete), NULL, NULL,  0, 0));
    APPEND(edit, sep_item());
    APPEND(edit, smi("cmd.selall",TM("cmd.42007", "Select _All"), G_CALLBACK(cb_selall), NULL, accel, GDK_KEY_a, GDK_CONTROL_MASK));
    APPEND(edit, sep_item());
    /* Copy to Clipboard submenu */
    {
        GtkWidget *ctc_item = gtk_menu_item_new_with_label("Copy to Clipboard");
        GtkWidget *ctc_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(ctc_item), ctc_menu);
        APPEND(ctc_menu, menu_item("Full Path",       G_CALLBACK(cb_copy_filepath), NULL, NULL, 0, 0));
        APPEND(ctc_menu, menu_item("File Name",       G_CALLBACK(cb_copy_filename), NULL, NULL, 0, 0));
        APPEND(ctc_menu, menu_item("Directory Path",  G_CALLBACK(cb_copy_dirpath),  NULL, NULL, 0, 0));
        APPEND(edit, ctc_item);
    }
    APPEND(edit, smi("cmd.indent",   TM("cmd.indent",   "_Indent"),          G_CALLBACK(cb_indent),   NULL, accel, GDK_KEY_Tab,       0));
    APPEND(edit, smi("cmd.unindent", TM("cmd.unindent", "_Unindent"),        G_CALLBACK(cb_unindent), NULL, accel, GDK_KEY_Tab, GDK_SHIFT_MASK));
    APPEND(edit, sep_item());
    APPEND(edit, menu_item(TM("menu.columneditor", "_Column Editor…"),
                           G_CALLBACK(cb_column_editor), NULL, accel,
                           GDK_KEY_c, GDK_MOD1_MASK));
    APPEND(edit, sep_item());

    /* EOL Conversion submenu */
    {
        GtkWidget *eol_sub_item = gtk_menu_item_new_with_mnemonic(TM("menu.eolformat", "EOL Con_version"));
        GtkWidget *eol_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(eol_sub_item), eol_menu);

        GSList *eol_group = NULL;
        s_eol_items[SC_EOL_CRLF] = gtk_radio_menu_item_new_with_mnemonic(eol_group,
            TM("menu.windows", "_Windows (CR+LF)"));
        eol_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(s_eol_items[SC_EOL_CRLF]));
        s_eol_items[SC_EOL_LF] = gtk_radio_menu_item_new_with_mnemonic(eol_group,
            TM("menu.unix", "_Unix (LF)"));
        eol_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(s_eol_items[SC_EOL_LF]));
        s_eol_items[SC_EOL_CR] = gtk_radio_menu_item_new_with_mnemonic(eol_group,
            TM("menu.oldmac", "Old _Mac (CR)"));

        g_signal_connect(s_eol_items[SC_EOL_CRLF], "toggled",
                         G_CALLBACK(cb_eol_toggled), GINT_TO_POINTER(SC_EOL_CRLF));
        g_signal_connect(s_eol_items[SC_EOL_LF], "toggled",
                         G_CALLBACK(cb_eol_toggled), GINT_TO_POINTER(SC_EOL_LF));
        g_signal_connect(s_eol_items[SC_EOL_CR], "toggled",
                         G_CALLBACK(cb_eol_toggled), GINT_TO_POINTER(SC_EOL_CR));

        APPEND(eol_menu, s_eol_items[SC_EOL_CRLF]);
        APPEND(eol_menu, s_eol_items[SC_EOL_LF]);
        APPEND(eol_menu, s_eol_items[SC_EOL_CR]);
        APPEND(edit, eol_sub_item);
    }

    /* Insert Date/Time submenu */
    {
        GtkWidget *dt_item = gtk_menu_item_new_with_mnemonic(TM("menu.datetime", "Insert _Date/Time"));
        GtkWidget *dt_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(dt_item), dt_menu);
        APPEND(dt_menu, menu_item(TM("menu.datetime.short", "_Short (HH:MM:SS MM/DD/YYYY)"),
                                  G_CALLBACK(cb_insert_date_short), NULL, NULL, 0, 0));
        APPEND(dt_menu, menu_item(TM("menu.datetime.long",  "_Long (Weekday, Month DD, YYYY HH:MM:SS)"),
                                  G_CALLBACK(cb_insert_date_long),  NULL, NULL, 0, 0));
        APPEND(edit, dt_item);
    }

    /* Line operations submenu */
    {
        GtkWidget *line_item = gtk_menu_item_new_with_mnemonic(TM("menu.line", "_Line Operations"));
        GtkWidget *line_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(line_item), line_menu);
        APPEND(line_menu, smi("cmd.line.dup",  TM("menu.line.duplicate", "_Duplicate Line"),
                                    G_CALLBACK(cb_line_duplicate), NULL, accel,
                                    GDK_KEY_d, GDK_CONTROL_MASK));
        APPEND(line_menu, smi("cmd.line.del",  TM("menu.line.delete", "D_elete Line"),
                                    G_CALLBACK(cb_line_delete), NULL, accel,
                                    GDK_KEY_l, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
        APPEND(line_menu, sep_item());
        APPEND(line_menu, smi("cmd.line.up",   TM("menu.line.moveup", "Move Line _Up"),
                                    G_CALLBACK(cb_line_move_up), NULL, accel,
                                    GDK_KEY_Up, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
        APPEND(line_menu, smi("cmd.line.down", TM("menu.line.movedown", "Move Line _Down"),
                                    G_CALLBACK(cb_line_move_down), NULL, accel,
                                    GDK_KEY_Down, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
        APPEND(line_menu, sep_item());
        APPEND(line_menu, menu_item(TM("menu.line.join",  "_Join Lines"),
                                    G_CALLBACK(cb_join_lines),  NULL, NULL, 0, 0));
        APPEND(line_menu, menu_item(TM("menu.line.split", "S_plit Lines"),
                                    G_CALLBACK(cb_split_lines), NULL, NULL, 0, 0));
        APPEND(line_menu, sep_item());
        APPEND(line_menu, smi("cmd.line.insabove", TM("menu.line.insabove", "Insert Blank Line A_bove"),
                                    G_CALLBACK(cb_line_insert_above), NULL, accel,
                                    GDK_KEY_Return, GDK_CONTROL_MASK | GDK_MOD1_MASK));
        APPEND(line_menu, smi("cmd.line.insbelow", TM("menu.line.insbelow", "Insert Blank Line Belo_w"),
                                    G_CALLBACK(cb_line_insert_below), NULL, accel,
                                    GDK_KEY_Return, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
        APPEND(line_menu, sep_item());
        APPEND(line_menu, menu_item(TM("menu.line.rmdup",   "Remove _Duplicate Lines"),
                                    G_CALLBACK(cb_remove_duplicate_lines), NULL, NULL, 0, 0));
        APPEND(line_menu, menu_item(TM("menu.line.rmblank", "Remove _Blank Lines"),
                                    G_CALLBACK(cb_remove_blank_lines),     NULL, NULL, 0, 0));
        APPEND(line_menu, sep_item());
        /* Sort Lines submenu */
        {
            GtkWidget *sort_item = gtk_menu_item_new_with_mnemonic(TM("menu.line.sort", "_Sort Lines"));
            GtkWidget *sort_menu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(sort_item), sort_menu);
            APPEND(sort_menu, menu_item(TM("menu.line.sort.lexic",    "_Lexicographic"),        G_CALLBACK(cb_sort_lexic),    NULL, NULL, 0, 0));
            APPEND(sort_menu, menu_item(TM("menu.line.sort.lexic_ci", "Lexicographic (_case-insensitive)"), G_CALLBACK(cb_sort_lexic_ci), NULL, NULL, 0, 0));
            APPEND(sort_menu, menu_item(TM("menu.line.sort.length",   "By _Length"),            G_CALLBACK(cb_sort_length),   NULL, NULL, 0, 0));
            APPEND(sort_menu, menu_item(TM("menu.line.sort.numeric",  "By _Number"),            G_CALLBACK(cb_sort_numeric),  NULL, NULL, 0, 0));
            APPEND(sort_menu, menu_item(TM("menu.line.sort.random",   "R_andom Shuffle"),       G_CALLBACK(cb_sort_random),   NULL, NULL, 0, 0));
            APPEND(sort_menu, sep_item());
            APPEND(sort_menu, menu_item(TM("menu.line.sort.reverse",  "Re_verse Order"),        G_CALLBACK(cb_sort_reverse),  NULL, NULL, 0, 0));
            APPEND(line_menu, sort_item);
        }
        APPEND(edit, line_item);
    }

    /* Blank Operations submenu */
    {
        GtkWidget *blank_item = gtk_menu_item_new_with_mnemonic(TM("menu.blank", "_Blank Operations"));
        GtkWidget *blank_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(blank_item), blank_menu);
        APPEND(blank_menu, menu_item(TM("menu.blank.trimtrail", "Trim _Trailing Whitespace"),
                                     G_CALLBACK(cb_trim_trailing), NULL, NULL, 0, 0));
        APPEND(blank_menu, menu_item(TM("menu.blank.trimlead",  "Trim _Leading Whitespace"),
                                     G_CALLBACK(cb_trim_leading),  NULL, NULL, 0, 0));
        APPEND(blank_menu, menu_item(TM("menu.blank.trimboth",  "Trim _Both"),
                                     G_CALLBACK(cb_trim_both),     NULL, NULL, 0, 0));
        APPEND(blank_menu, sep_item());
        APPEND(blank_menu, menu_item(TM("menu.blank.spacestotabs", "Convert _Spaces to Tabs"),
                                     G_CALLBACK(cb_spaces_to_tabs), NULL, NULL, 0, 0));
        APPEND(blank_menu, menu_item(TM("menu.blank.tabstospaces", "Convert _Tabs to Spaces"),
                                     G_CALLBACK(cb_tabs_to_spaces), NULL, NULL, 0, 0));
        APPEND(edit, blank_item);
    }

    /* Convert Case To submenu */
    {
        GtkWidget *case_item = gtk_menu_item_new_with_mnemonic(TM("menu.case", "Co_nvert Case To"));
        GtkWidget *case_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(case_item), case_menu);
        APPEND(case_menu, menu_item(TM("menu.case.upper",    "_UPPER CASE"),    G_CALLBACK(cb_case_upper),    NULL, NULL, 0, 0));
        APPEND(case_menu, menu_item(TM("menu.case.lower",    "_lower case"),    G_CALLBACK(cb_case_lower),    NULL, NULL, 0, 0));
        APPEND(case_menu, menu_item(TM("menu.case.proper",   "_Proper Case"),   G_CALLBACK(cb_case_proper),   NULL, NULL, 0, 0));
        APPEND(case_menu, menu_item(TM("menu.case.sentence", "_Sentence case"), G_CALLBACK(cb_case_sentence), NULL, NULL, 0, 0));
        APPEND(case_menu, menu_item(TM("menu.case.invert",   "_iNVERT cASE"),   G_CALLBACK(cb_case_invert),   NULL, NULL, 0, 0));
        APPEND(case_menu, menu_item(TM("menu.case.random",   "_rAnDoM cAsE"),   G_CALLBACK(cb_case_random),   NULL, NULL, 0, 0));
        APPEND(edit, case_item);
    }

    /* Comment / Uncomment submenu */
    {
        GtkWidget *cmt_item = gtk_menu_item_new_with_mnemonic(TM("menu.comment", "C_omment/Uncomment"));
        GtkWidget *cmt_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(cmt_item), cmt_menu);
        APPEND(cmt_menu, smi("cmd.comment.line",  TM("menu.comment.line",  "Toggle _Single Line Comment"),
                                    G_CALLBACK(cb_toggle_line_comment),  NULL, accel,
                                    GDK_KEY_k, GDK_CONTROL_MASK));
        APPEND(cmt_menu, smi("cmd.comment.block", TM("menu.comment.block", "Toggle _Block Comment"),
                                    G_CALLBACK(cb_toggle_block_comment), NULL, accel,
                                    GDK_KEY_k, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
        APPEND(edit, cmt_item);
    }

    /* On Selection submenu */
    {
        GtkWidget *sel_item = gtk_menu_item_new_with_label("On Selection");
        GtkWidget *sel_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(sel_item), sel_menu);
        APPEND(sel_menu, nyi_item("Open File"));
        APPEND(sel_menu, nyi_item("Open Folder"));
        APPEND(sel_menu, sep_item());
        APPEND(sel_menu, nyi_item("Google Search"));
        APPEND(sel_menu, nyi_item("Wikipedia Search"));
        APPEND(sel_menu, nyi_item("Stack Overflow Search"));
        APPEND(edit, sel_item);
    }
    APPEND(edit, sep_item());
    APPEND(edit, nyi_item("Character Panel…"));
    APPEND(edit, sep_item());
    APPEND(edit, nyi_item("Read-Only"));
    APPEND(edit, nyi_item("Clear Read-Only Flag"));

    /* ---- Search ---- */
    GtkWidget *search = submenu(bar, TM("menu.search", "_Search"));
    APPEND(search, smi("cmd.find",       TM("cmd.43001", "_Find…"),          G_CALLBACK(cb_find),         NULL, accel, GDK_KEY_f, GDK_CONTROL_MASK));
    APPEND(search, smi("cmd.replace",    TM("cmd.43003", "_Replace…"),       G_CALLBACK(cb_replace),      NULL, accel, GDK_KEY_h, GDK_CONTROL_MASK));
    APPEND(search, smi("cmd.findinfiles",TM("cmd.findinfiles","Find in _Files…"), G_CALLBACK(cb_find_in_files), NULL, accel, GDK_KEY_f, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    APPEND(search, nyi_item("Incremental Search…"));
    APPEND(search, sep_item());
    APPEND(search, smi("cmd.findnext",   TM("cmd.findnext",  "Find _Next"),   G_CALLBACK(cb_find_next), NULL, accel, GDK_KEY_F3, 0));
    APPEND(search, smi("cmd.findprev",   TM("cmd.findprev",  "Find _Prev"),   G_CALLBACK(cb_find_prev), NULL, accel, GDK_KEY_F3, GDK_SHIFT_MASK));
    APPEND(search, sep_item());
    /* Change History submenu */
    {
        GtkWidget *ch_item = gtk_menu_item_new_with_label("Change History");
        GtkWidget *ch_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(ch_item), ch_menu);
        APPEND(ch_menu, nyi_item("Next Change"));
        APPEND(ch_menu, nyi_item("Previous Change"));
        APPEND(ch_menu, sep_item());
        APPEND(ch_menu, nyi_item("Revert Recent Change"));
        APPEND(ch_menu, nyi_item("Clear All Changes"));
        APPEND(search, ch_item);
    }
    APPEND(search, sep_item());
    APPEND(search, smi("cmd.selectall.occ", TM("cmd.selectall.occ", "Select _All Occurrences"),
                       G_CALLBACK(cb_select_all_occurrences), NULL, accel,
                       GDK_KEY_a, GDK_CONTROL_MASK | GDK_MOD1_MASK));
    APPEND(search, smi("cmd.addnext.occ",   TM("cmd.addnext.occ",   "Add _Next Occurrence"),
                       G_CALLBACK(cb_add_next_occurrence),    NULL, accel,
                       GDK_KEY_d, GDK_CONTROL_MASK | GDK_MOD1_MASK));
    APPEND(search, sep_item());
    APPEND(search, smi("cmd.goto",   TM("cmd.43004", "_Go To Line…"), G_CALLBACK(cb_goto),    NULL, accel, GDK_KEY_g, GDK_CONTROL_MASK));
    APPEND(search, smi("cmd.brace",  TM("cmd.brace", "Go to _Matching Brace"), G_CALLBACK(cb_goto_matching_brace), NULL, accel, GDK_KEY_bracketright, GDK_CONTROL_MASK));
    APPEND(search, sep_item());
    APPEND(search, smi("cmd.bm.toggle", TM("menu.bm.toggle", "_Toggle Bookmark"),
                             G_CALLBACK(cb_bookmark_toggle), NULL, accel,
                             GDK_KEY_F2, GDK_CONTROL_MASK));
    APPEND(search, smi("cmd.bm.next",   TM("menu.bm.next", "_Next Bookmark"),
                             G_CALLBACK(cb_bookmark_next), NULL, accel,
                             GDK_KEY_F2, 0));
    APPEND(search, smi("cmd.bm.prev",   TM("menu.bm.prev", "_Previous Bookmark"),
                             G_CALLBACK(cb_bookmark_prev), NULL, accel,
                             GDK_KEY_F2, GDK_SHIFT_MASK));
    APPEND(search, menu_item(TM("menu.bm.clearall", "_Clear All Bookmarks"),
                             G_CALLBACK(cb_bookmark_clear_all), NULL, NULL, 0, 0));
    APPEND(search, sep_item());
    APPEND(search, menu_item(TM("menu.bm.cut",    "C_ut Bookmarked Lines"),
                             G_CALLBACK(cb_bookmark_cut),    NULL, NULL, 0, 0));
    APPEND(search, menu_item(TM("menu.bm.copy",   "Cop_y Bookmarked Lines"),
                             G_CALLBACK(cb_bookmark_copy),   NULL, NULL, 0, 0));
    APPEND(search, menu_item(TM("menu.bm.delete", "_Delete Bookmarked Lines"),
                             G_CALLBACK(cb_bookmark_delete), NULL, NULL, 0, 0));
    APPEND(search, sep_item());

    /* Mark styles submenu */
    {
        static GCallback apply_cbs[5]      = { G_CALLBACK(cb_mark1), G_CALLBACK(cb_mark2), G_CALLBACK(cb_mark3), G_CALLBACK(cb_mark4), G_CALLBACK(cb_mark5) };
        static GCallback clear_cbs[5]      = { G_CALLBACK(cb_mark_clear1), G_CALLBACK(cb_mark_clear2), G_CALLBACK(cb_mark_clear3), G_CALLBACK(cb_mark_clear4), G_CALLBACK(cb_mark_clear5) };
        static GCallback next_cbs[5]       = { G_CALLBACK(cb_mark_next1), G_CALLBACK(cb_mark_next2), G_CALLBACK(cb_mark_next3), G_CALLBACK(cb_mark_next4), G_CALLBACK(cb_mark_next5) };
        static GCallback prev_cbs[5]       = { G_CALLBACK(cb_mark_prev1), G_CALLBACK(cb_mark_prev2), G_CALLBACK(cb_mark_prev3), G_CALLBACK(cb_mark_prev4), G_CALLBACK(cb_mark_prev5) };

        GtkWidget *mark_item = gtk_menu_item_new_with_mnemonic(TM("menu.mark", "_Mark Styles"));
        GtkWidget *mark_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mark_item), mark_menu);

        for (int s = 0; s < MARK_STYLE_COUNT; s++) {
            char label[64];
            /* Apply */
            snprintf(label, sizeof(label), "_Mark Style %d", s + 1);
            APPEND(mark_menu, menu_item(label, apply_cbs[s], NULL, NULL, 0, 0));
        }
        APPEND(mark_menu, sep_item());
        for (int s = 0; s < MARK_STYLE_COUNT; s++) {
            char label[64];
            snprintf(label, sizeof(label), "Clear Style _%d", s + 1);
            APPEND(mark_menu, menu_item(label, clear_cbs[s], NULL, NULL, 0, 0));
        }
        APPEND(mark_menu, menu_item(TM("menu.mark.clearall", "Clear _All Marks"),
                                    G_CALLBACK(cb_mark_clear_all), NULL, NULL, 0, 0));
        APPEND(mark_menu, sep_item());
        for (int s = 0; s < MARK_STYLE_COUNT; s++) {
            char label[64];
            snprintf(label, sizeof(label), "Next Style %d", s + 1);
            APPEND(mark_menu, menu_item(label, next_cbs[s], NULL, NULL, 0, 0));
        }
        APPEND(mark_menu, sep_item());
        for (int s = 0; s < MARK_STYLE_COUNT; s++) {
            char label[64];
            snprintf(label, sizeof(label), "Previous Style %d", s + 1);
            APPEND(mark_menu, menu_item(label, prev_cbs[s], NULL, NULL, 0, 0));
        }
        APPEND(search, mark_item);
    }

    /* ---- View ---- */
    {
        GtkWidget *view = submenu(bar, TM("menu.view", "_View"));

        s_mi_wrap = gtk_check_menu_item_new_with_mnemonic(
            TM("menu.view.wordwrap", "_Word Wrap"));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s_mi_wrap), FALSE);
        g_signal_connect(s_mi_wrap, "toggled", G_CALLBACK(cb_toggle_word_wrap), NULL);
        gtk_accel_group_connect(accel, GDK_KEY_z, GDK_MOD1_MASK, GTK_ACCEL_VISIBLE,
            g_cclosure_new(G_CALLBACK(cb_toggle_word_wrap), NULL, NULL));
        APPEND(view, s_mi_wrap);

        APPEND(view, sep_item());

        GtkWidget *ws = gtk_check_menu_item_new_with_mnemonic(
            TM("menu.view.whitespace", "Show _Whitespace"));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ws), s_show_whitespace);
        g_signal_connect(ws, "toggled", G_CALLBACK(cb_toggle_whitespace), NULL);
        APPEND(view, ws);

        GtkWidget *eolm = gtk_check_menu_item_new_with_mnemonic(
            TM("menu.view.eol", "Show _EOL Markers"));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(eolm), s_show_eol_marks);
        g_signal_connect(eolm, "toggled", G_CALLBACK(cb_toggle_eol_marks), NULL);
        APPEND(view, eolm);

        APPEND(view, sep_item());

        GtkWidget *ln = gtk_check_menu_item_new_with_mnemonic(
            TM("menu.view.linenums", "Show _Line Numbers"));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ln), s_show_linenums);
        g_signal_connect(ln, "toggled", G_CALLBACK(cb_toggle_linenums), NULL);
        APPEND(view, ln);

        GtkWidget *fm = gtk_check_menu_item_new_with_mnemonic(
            TM("menu.view.fold", "Show _Fold Margin"));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(fm), s_show_fold);
        g_signal_connect(fm, "toggled", G_CALLBACK(cb_toggle_fold), NULL);
        APPEND(view, fm);

        GtkWidget *bm = gtk_check_menu_item_new_with_mnemonic(
            TM("menu.view.bookmarks", "Show _Bookmarks Margin"));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bm), s_show_bookmarks);
        g_signal_connect(bm, "toggled", G_CALLBACK(cb_toggle_bookmarks), NULL);
        APPEND(view, bm);

        APPEND(view, sep_item());

        GtkWidget *edge = gtk_check_menu_item_new_with_mnemonic(
            TM("menu.view.edge", "Show _Edge Column"));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(edge), s_edge_enabled);
        g_signal_connect(edge, "toggled", G_CALLBACK(cb_toggle_edge), NULL);
        APPEND(view, edge);

        APPEND(view, menu_item(TM("menu.view.setedge", "Set Edge Column…"),
                               G_CALLBACK(cb_set_edge_column), NULL, NULL, 0, 0));

        APPEND(view, sep_item());

        /* Folding submenu */
        GtkWidget *fold_sub  = gtk_menu_new();
        GtkWidget *fold_item = gtk_menu_item_new_with_mnemonic(
            TM("menu.view.folding", "_Folding"));
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(fold_item), fold_sub);
        APPEND(view, fold_item);

        APPEND(fold_sub, menu_item(TM("menu.view.foldall",   "Fold _All"),
                                   G_CALLBACK(cb_fold_all),   NULL, accel,
                                   GDK_KEY_F9, GDK_MOD1_MASK | GDK_CONTROL_MASK));
        APPEND(fold_sub, menu_item(TM("menu.view.unfoldall", "_Unfold All"),
                                   G_CALLBACK(cb_unfold_all), NULL, accel,
                                   GDK_KEY_F9, GDK_MOD1_MASK | GDK_CONTROL_MASK | GDK_SHIFT_MASK));
        APPEND(fold_sub, sep_item());
        APPEND(fold_sub, smi("cmd.fold.current",   TM("menu.view.foldcurrent",   "Fold _Current Level"),
                             G_CALLBACK(cb_fold_current),   NULL, accel, GDK_KEY_F9, GDK_CONTROL_MASK));
        APPEND(fold_sub, smi("cmd.unfold.current", TM("menu.view.unfoldcurrent", "_Unfold Current Level"),
                             G_CALLBACK(cb_unfold_current), NULL, accel, GDK_KEY_F9, GDK_CONTROL_MASK | GDK_SHIFT_MASK));

        APPEND(fold_sub, sep_item());

        /* Fold / Unfold Level 1–8 */
        for (int lv = 1; lv <= 8; lv++) {
            char lbl_fold[32], lbl_unfold[32];
            snprintf(lbl_fold,   sizeof(lbl_fold),   "Fold Level _%d", lv);
            snprintf(lbl_unfold, sizeof(lbl_unfold), "Unfold Level %d", lv);
            APPEND(fold_sub, menu_item(lbl_fold,
                                       G_CALLBACK(cb_fold_level),
                                       GINT_TO_POINTER(lv), NULL, 0, 0));
            APPEND(fold_sub, menu_item(lbl_unfold,
                                       G_CALLBACK(cb_unfold_level),
                                       GINT_TO_POINTER(lv), NULL, 0, 0));
            if (lv < 8) APPEND(fold_sub, sep_item());
        }

        APPEND(view, sep_item());

        /* Tab navigation submenu */
        {
            GtkWidget *tab_item = gtk_menu_item_new_with_label("Tab");
            GtkWidget *tab_menu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(tab_item), tab_menu);
            APPEND(tab_menu, smi("cmd.tab.next",  "Next Tab",           G_CALLBACK(cb_next_tab),  NULL, accel, GDK_KEY_Tab,       GDK_CONTROL_MASK));
            APPEND(tab_menu, smi("cmd.tab.prev",  "Previous Tab",       G_CALLBACK(cb_prev_tab),  NULL, accel, GDK_KEY_Tab,       GDK_CONTROL_MASK | GDK_SHIFT_MASK));
            APPEND(tab_menu, smi("cmd.tab.first", "First Tab",          G_CALLBACK(cb_first_tab), NULL, accel, GDK_KEY_Page_Up,   GDK_CONTROL_MASK | GDK_SHIFT_MASK));
            APPEND(tab_menu, smi("cmd.tab.last",  "Last Tab",           G_CALLBACK(cb_last_tab),  NULL, accel, GDK_KEY_Page_Down, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
            APPEND(tab_menu, sep_item());
            for (int t = 1; t <= 9; t++) {
                char lbl[24];
                snprintf(lbl, sizeof(lbl), "Tab %d", t);
                APPEND(tab_menu, menu_item(lbl, G_CALLBACK(cb_select_tab_n), GINT_TO_POINTER(t), NULL, 0, 0));
            }
            APPEND(view, tab_item);
        }

        APPEND(view, sep_item());

        /* Zoom submenu */
        {
            GtkWidget *zoom_item = gtk_menu_item_new_with_mnemonic(TM("menu.view.zoom", "_Zoom"));
            GtkWidget *zoom_menu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(zoom_item), zoom_menu);
            APPEND(zoom_menu, smi("cmd.zoom.in",      "Zoom In",              G_CALLBACK(cb_zoom_in),      NULL, accel, GDK_KEY_equal, GDK_CONTROL_MASK));
            APPEND(zoom_menu, smi("cmd.zoom.out",     "Zoom Out",             G_CALLBACK(cb_zoom_out),     NULL, accel, GDK_KEY_minus, GDK_CONTROL_MASK));
            APPEND(zoom_menu, smi("cmd.zoom.restore", "Restore Default Zoom", G_CALLBACK(cb_zoom_restore), NULL, accel, GDK_KEY_0, GDK_CONTROL_MASK));
            APPEND(view, zoom_item);
        }

        APPEND(view, sep_item());

        /* Panels submenu (placeholders) */
        {
            GtkWidget *pan_item = gtk_menu_item_new_with_label("Panels");
            GtkWidget *pan_menu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(pan_item), pan_menu);
            {
                GtkWidget *mi_doclist = gtk_check_menu_item_new_with_label("Document List");
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi_doclist), FALSE);
                g_signal_connect(mi_doclist, "toggled", G_CALLBACK(cb_toggle_doclist), NULL);
                APPEND(pan_menu, mi_doclist);
            }
            APPEND(pan_menu, nyi_item("Document Map"));
            {
                GtkWidget *mi_fl = gtk_check_menu_item_new_with_label("Function List");
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi_fl), FALSE);
                g_signal_connect(mi_fl, "toggled", G_CALLBACK(cb_toggle_funclist), NULL);
                APPEND(pan_menu, mi_fl);
            }
            {
                GtkWidget *mi_ws = gtk_check_menu_item_new_with_label("Folder as Workspace");
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi_ws), FALSE);
                g_signal_connect(mi_ws, "toggled", G_CALLBACK(cb_toggle_workspace), NULL);
                APPEND(pan_menu, mi_ws);
            }
            APPEND(pan_menu, nyi_item("Project Manager"));
            APPEND(pan_menu, nyi_item("Monitoring (tail -f)"));
            APPEND(view, pan_item);
        }

        APPEND(view, sep_item());

        /* Synchronise submenu */
        {
            GtkWidget *sync_item = gtk_menu_item_new_with_label("Synchronise");
            GtkWidget *sync_menu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(sync_item), sync_menu);
            APPEND(sync_menu, nyi_item("Synchronise Horizontal Scrolling"));
            APPEND(sync_menu, nyi_item("Synchronise Vertical Scrolling"));
            APPEND(view, sync_item);
        }

        APPEND(view, sep_item());

        /* Always on Top */
        {
            GtkWidget *aot = gtk_check_menu_item_new_with_label("Always on Top");
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(aot), FALSE);
            g_signal_connect(aot, "toggled", G_CALLBACK(cb_always_on_top), NULL);
            APPEND(view, aot);
        }
        APPEND(view, nyi_item("Text Direction RTL"));
        APPEND(view, nyi_item("Text Direction LTR"));
    }

    /* ---- Language ---- */
    build_language_menu(bar);

    /* ---- Encoding ---- */
    {
        GtkWidget *enc_menu = submenu(bar, T("menu.encoding", "_Encoding"));
        GSList *enc_group = NULL;

        /* UTF group */
        for (int i = 0; i < 6 && i < npp_encoding_count; i++) {
            GtkWidget *mi = gtk_radio_menu_item_new_with_label(enc_group,
                                                               npp_encodings[i].display);
            enc_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(mi));
            s_enc_items[i] = mi;
            g_signal_connect(mi, "activate", G_CALLBACK(cb_set_encoding),
                             (gpointer)npp_encodings[i].display);
            APPEND(enc_menu, mi);
            if (i == 1) APPEND(enc_menu, sep_item()); /* after UTF-8 BOM */
            if (i == 5) APPEND(enc_menu, sep_item()); /* after UTF-16 BE BOM */
        }

        /* Western European */
        GtkWidget *we_item = gtk_menu_item_new_with_label("Western European");
        GtkWidget *we_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(we_item), we_menu);
        APPEND(enc_menu, we_item);

        /* Central European */
        GtkWidget *ce_item = gtk_menu_item_new_with_label("Central European");
        GtkWidget *ce_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(ce_item), ce_menu);
        APPEND(enc_menu, ce_item);

        /* Cyrillic */
        GtkWidget *cy_item = gtk_menu_item_new_with_label("Cyrillic");
        GtkWidget *cy_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(cy_item), cy_menu);
        APPEND(enc_menu, cy_item);

        /* East Asian */
        GtkWidget *ea_item = gtk_menu_item_new_with_label("East Asian");
        GtkWidget *ea_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(ea_item), ea_menu);
        APPEND(enc_menu, ea_item);

        /* Fill regional submenus — indices 6-16 in npp_encodings[] */
        static const int we_idx[] = { 6, 7, 8 };       /* Win-1252, 8859-1, 8859-15 */
        static const int ce_idx[] = { 9, 10 };          /* Win-1250, 8859-2 */
        static const int cy_idx[] = { 11, 12 };         /* Win-1251, KOI8-R */
        static const int ea_idx[] = { 13, 14, 15, 16 }; /* Shift-JIS, GB18030, Big5, EUC-KR */
        struct { GtkWidget *menu; const int *idx; int cnt; } groups[] = {
            { we_menu, we_idx, 3 },
            { ce_menu, ce_idx, 2 },
            { cy_menu, cy_idx, 2 },
            { ea_menu, ea_idx, 4 },
        };
        for (int g = 0; g < 4; g++) {
            for (int j = 0; j < groups[g].cnt; j++) {
                int i = groups[g].idx[j];
                if (i >= npp_encoding_count) break;
                GtkWidget *mi = gtk_radio_menu_item_new_with_label(enc_group,
                                                                   npp_encodings[i].display);
                enc_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(mi));
                s_enc_items[i] = mi;
                g_signal_connect(mi, "activate", G_CALLBACK(cb_set_encoding),
                                 (gpointer)npp_encodings[i].display);
                APPEND(groups[g].menu, mi);
            }
        }

        /* Default: UTF-8 active */
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s_enc_items[0]), TRUE);
    }

    /* ---- Settings ---- */
    GtkWidget *settings = submenu(bar, TM("menu.settings", "Se_ttings"));
    APPEND(settings, menu_item(TM("cmd.46001", "_Style Configurator…"),
                               G_CALLBACK(cb_style_editor), NULL, accel, 0, 0));
    APPEND(settings, menu_item(T("cmd.shortcutmap", "_Shortcut Mapper…"),
                               G_CALLBACK(cb_shortcut_mapper), NULL, accel, 0, 0));
    APPEND(settings, sep_item());
    APPEND(settings, menu_item(TM("cmd.prefs", "_Preferences…"),
                               G_CALLBACK(cb_preferences), NULL, accel, 0, 0));
    APPEND(settings, sep_item());
    /* Import submenu */
    {
        GtkWidget *imp_item = gtk_menu_item_new_with_label("Import");
        GtkWidget *imp_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(imp_item), imp_menu);
        APPEND(imp_menu, nyi_item("Import Plugin(s)…"));
        APPEND(imp_menu, nyi_item("Import Style Themes(s)…"));
        APPEND(settings, imp_item);
    }
    APPEND(settings, sep_item());
    APPEND(settings, nyi_item("Edit Context Menu…"));

    /* ---- Tools ---- */
    GtkWidget *tools = submenu(bar, TM("menu.tools", "_Tools"));
    APPEND(tools, menu_item(TM("menu.tools.hash", "_Hash Generator…"),
                            G_CALLBACK(cb_hash_generator), NULL, accel, 0, 0));
    APPEND(tools, sep_item());
    APPEND(tools, menu_item(TM("menu.tools.b64enc", "Base64 _Encode"),
                            G_CALLBACK(cb_base64_encode), NULL, accel, 0, 0));
    APPEND(tools, menu_item(TM("menu.tools.b64dec", "Base64 _Decode"),
                            G_CALLBACK(cb_base64_decode), NULL, accel, 0, 0));
    APPEND(tools, sep_item());
    APPEND(tools, menu_item(TM("menu.tools.asctohex", "ASCII → _Hex"),
                            G_CALLBACK(cb_ascii_to_hex), NULL, accel, 0, 0));
    APPEND(tools, menu_item(TM("menu.tools.hextoasc", "_Hex → ASCII"),
                            G_CALLBACK(cb_hex_to_ascii), NULL, accel, 0, 0));

    /* ---- Macro ---- */
    {
        GtkWidget *macro = submenu(bar, TM("menu.macro", "_Macro"));
        APPEND(macro, smi("cmd.macro.start", TM("menu.macro.start", "Start _Recording"),
                          G_CALLBACK(cb_macro_start), NULL, accel,
                          GDK_KEY_r, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
        APPEND(macro, smi("cmd.macro.stop",  TM("menu.macro.stop",  "S_top Recording"),
                          G_CALLBACK(cb_macro_stop),  NULL, accel,
                          GDK_KEY_r, GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK));
        APPEND(macro, sep_item());
        APPEND(macro, smi("cmd.macro.play",  TM("menu.macro.play",  "_Playback"),
                          G_CALLBACK(cb_macro_play),  NULL, accel,
                          GDK_KEY_p, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
        APPEND(macro, smi("cmd.macro.playn", TM("menu.macro.playn", "Run Macro _Multiple Times…"),
                          G_CALLBACK(cb_macro_play_n), NULL, accel,
                          GDK_KEY_p, GDK_MOD1_MASK | GDK_SHIFT_MASK));
        APPEND(macro, sep_item());
        APPEND(macro, nyi_item("Save Current Recorded Macro As…"));
        APPEND(macro, nyi_item("Trim Trailing Space and Save"));
        APPEND(macro, sep_item());
        APPEND(macro, nyi_item("Modify Shortcut / Delete Macro…"));
    }

    /* ---- Run ---- */
    {
        GtkWidget *run = submenu(bar, TM("menu.run", "_Run"));
        APPEND(run, nyi_item("Run…"));
        APPEND(run, sep_item());
        APPEND(run, nyi_item("Modify Shortcut / Delete Command…"));
    }

    /* ---- Plugins ---- */
    {
        GtkWidget *plugins = submenu(bar, TM("menu.plugins", "_Plugins"));
        APPEND(plugins, nyi_item("Plugins Admin…"));
    }

    /* ---- Help ---- */
    {
        GtkWidget *help = submenu(bar, TM("menu.help", "_Help"));
        APPEND(help, nyi_item("About Notepad++ Linux…"));
        APPEND(help, nyi_item("Debug Info…"));
        APPEND(help, sep_item());
        APPEND(help, nyi_item("Project Home Page"));
        APPEND(help, nyi_item("Online Documentation"));
        APPEND(help, sep_item());
        APPEND(help, nyi_item("Check for Updates…"));
    }

    return bar;
}

/* ------------------------------------------------------------------ */
/* Tab switch                                                         */
/* ------------------------------------------------------------------ */

static void on_switch_page(GtkNotebook *nb, GtkWidget *page,
                           guint n, gpointer d)
{
    (void)nb; (void)page; (void)d;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    statusbar_update_from_sci(doc->sci);
    const char *lang = (const char *)g_object_get_data(G_OBJECT(doc->sci), "npp-lang");
    lang_menu_sync(lang);
    int eol = (int)scintilla_send_message(SCINTILLA(doc->sci), SCI_GETEOLMODE, 0, 0);
    eol_menu_sync(eol);
    apply_view_symbols(doc->sci);
    apply_edge(doc->sci);
    wrap_menu_sync(doc->word_wrap);
    toolbar_sync_toggles(doc->sci);
    main_sync_encoding_menu(doc->encoding ? doc->encoding : "UTF-8");
    doclist_sync_selection((int)n);
    funclist_update(doc->sci);
}

/* ------------------------------------------------------------------ */
/* Insert key — toggle overtype mode                                  */
/* ------------------------------------------------------------------ */

static gboolean on_key_press(GtkWidget *w, GdkEventKey *ev, gpointer d)
{
    (void)w; (void)d;
    if (ev->keyval != GDK_KEY_Insert) return FALSE;
    NppDoc *doc = editor_current_doc();
    if (!doc) return FALSE;
    gboolean ovr = (gboolean)scintilla_send_message(
        SCINTILLA(doc->sci), SCI_GETOVERTYPE, 0, 0);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_SETOVERTYPE, !ovr, 0);
    statusbar_set_overtype(!ovr);
    return TRUE;  /* consumed — prevents Scintilla from double-toggling */
}

/* ------------------------------------------------------------------ */
/* Delete-event (window X button)                                     */
/* ------------------------------------------------------------------ */

static gboolean on_delete_event(GtkWidget *w, GdkEvent *e, gpointer app)
{
    (void)w; (void)e;
    session_save();
    editor_close_all_quit(G_APPLICATION(app));
    return TRUE; /* prevent default destroy; quit handles it */
}

/* ------------------------------------------------------------------ */
/* Application activate                                               */
/* ------------------------------------------------------------------ */

static void on_activate(GtkApplication *app, gpointer data)
{
    (void)data;

    i18n_init();

    s_recent_files = g_ptr_array_new();
    recent_load();
    prefs_load();
    shortcut_load();

    GtkWidget *window = gtk_application_window_new(app);
    s_main_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Notepad++ Linux");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 700);
    g_signal_connect(window, "delete-event",   G_CALLBACK(on_delete_event), app);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press),   NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Menu bar */
    GtkWidget *menubar = build_menubar(GTK_WINDOW(window), G_APPLICATION(app));
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    /* Toolbar */
    GtkWidget *toolbar = toolbar_init(window);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    /* Editor area layout (left-to-right):
     *   [workspace] | [doclist] | [editor notebook] | [funclist]
     * Each panel lives as pack1/pack2 of its own GtkPaned so it
     * collapses cleanly when hidden.                               */
    GtkWidget *notebook  = editor_init(window);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(on_switch_page), NULL);

    /* notebook | funclist */
    GtkWidget *right_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(right_paned), notebook,       TRUE,  TRUE);
    gtk_paned_pack2(GTK_PANED(right_paned), funclist_init(), FALSE, FALSE);

    /* doclist | (notebook + funclist) */
    GtkWidget *inner_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(inner_paned), doclist_init(), FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(inner_paned), right_paned,    TRUE,  TRUE);

    /* workspace | rest */
    GtkWidget *outer_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(outer_paned), workspace_init(window), FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(outer_paned), inner_paned,            TRUE,  TRUE);

    gtk_box_pack_start(GTK_BOX(vbox), outer_paned, TRUE, TRUE, 0);
    backup_init();

    /* Status bar */
    GtkWidget *statusbar = statusbar_init();
    gtk_box_pack_start(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);

    /* Open files passed on the command line */
    const gchar **args = g_application_get_dbus_object_path(G_APPLICATION(app))
        ? NULL : NULL;
    (void)args; /* CLI args handled below in main() via editor_open_path */

    gtk_widget_show_all(window);
    /* show_all overrides any hide() set before realisation; re-hide side panels */
    doclist_set_visible(FALSE);
    workspace_set_visible(FALSE);
    funclist_set_visible(FALSE);

    /* Restore previous session (only when no files given on CLI) */
    if (s_restore_session)
        session_restore();

    NppDoc *initial = editor_current_doc();
    statusbar_update_from_sci(initial->sci);
    lang_menu_sync((const char *)g_object_get_data(G_OBJECT(initial->sci), "npp-lang"));
    eol_menu_sync((int)scintilla_send_message(SCINTILLA(initial->sci), SCI_GETEOLMODE, 0, 0));
    apply_view_symbols(initial->sci);
    apply_edge(initial->sci);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    /* Restore last session only when launched with no file arguments */
    s_restore_session = (argc == 1);

    GtkApplication *app = gtk_application_new("org.notepadplusplus.linux",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    /* Pass only argv[0] so GTK doesn't try to handle our file args */
    int status = g_application_run(G_APPLICATION(app), 1, argv);

    /* Open files given on the command line after the window is up */
    if (status == 0 && argc > 1) {
        for (int i = 1; i < argc; i++)
            editor_open_path(argv[i]);
    }

    g_object_unref(app);
    scintilla_release_resources();
    return status;
}
