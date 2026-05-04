#ifndef EDITOR_H
#define EDITOR_H

#include <gtk/gtk.h>
#include "sci_c.h"

typedef struct {
    GtkWidget *sci;
    char      *filepath;    /* NULL = unsaved */
    char      *encoding;    /* e.g. "UTF-8", "ISO-8859-1" — see npp_encodings[] */
    gboolean   modified;
    int        new_index;   /* "new N" label when filepath==NULL */
    gboolean   word_wrap;   /* per-tab word wrap state */
} NppDoc;

/* Initialise — call once, returns the GtkNotebook to embed in the window */
GtkWidget *editor_init(GtkWidget *window);

/* Document access */
NppDoc    *editor_current_doc(void);
NppDoc    *editor_doc_at(int page);
int        editor_page_count(void);
int        editor_current_page(void);
GtkWidget *editor_get_notebook(void);

/* File operations (dialogs shown when path is NULL / as appropriate) */
void       editor_new_doc(void);
gboolean   editor_open_dialog(void);               /* shows GTK open dialog */
gboolean   editor_open_path(const char *path);     /* open a specific file   */
gboolean   editor_save(void);                      /* save current doc       */
gboolean   editor_save_at(int page);               /* save specific page     */
gboolean   editor_save_as_dialog(void);            /* shows GTK save dialog  */
gboolean   editor_close_page(int page);            /* -1 = current           */
void       editor_close_all_quit(GApplication *app);

/* Edit operations on current document */
void editor_undo(void);
void editor_redo(void);
void editor_cut(void);
void editor_copy(void);
void editor_paste(void);
void editor_select_all(void);
void editor_goto_line_dialog(void);

/* Re-apply current theme styles to all open editors. */
void editor_reapply_styles(void);

/* Re-apply g_prefs to all open editors (call after changing preferences). */
void editor_apply_prefs(void);

/* Implemented in main.c, called from editor.c */
void main_toggle_bookmark_at_line(GtkWidget *sci, int line);
void main_recent_file_add(const char *path);
void main_sync_encoding_menu(const char *enc);
void main_apply_view_symbols(GtkWidget *sci); /* apply margin widths to one sci */

/* Open a file (or switch to it if already open) then jump to a 1-based line. */
void   editor_open_and_goto(const char *path, int line);

/* Convenience send to current doc */
sptr_t editor_send(unsigned int msg, uptr_t wp, sptr_t lp);

#endif /* EDITOR_H */
