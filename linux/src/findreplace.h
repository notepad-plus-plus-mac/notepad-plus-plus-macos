#ifndef FINDREPLACE_H
#define FINDREPLACE_H

#include <gtk/gtk.h>

/* Show (or raise) the Find/Replace dialog.
 * parent_window: the main application window (used for positioning).
 * find_text:     pre-fill the "Find what" field if non-NULL.
 * show_replace:  TRUE to show the Replace widgets, FALSE for Find-only. */
void findreplace_show(GtkWidget *parent_window, const char *find_text, gboolean show_replace);

/* Must be called whenever the active Scintilla widget changes. */
void findreplace_set_sci(GtkWidget *sci);

/* Repeat the last search forward / backward without opening the dialog. */
void findreplace_find_next(void);
void findreplace_find_prev(void);

#endif /* FINDREPLACE_H */
