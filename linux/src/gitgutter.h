#ifndef GITGUTTER_H
#define GITGUTTER_H

#include <gtk/gtk.h>

/* Margin index and marker numbers reserved for the git gutter */
#define GG_MARGIN       3
#define GG_MARK_ADDED   2
#define GG_MARK_MODIFIED 3
#define GG_MARK_DELETED  4
#define GG_MASK         ((1 << GG_MARK_ADDED) | (1 << GG_MARK_MODIFIED) | (1 << GG_MARK_DELETED))

/* Set up margin 3 and define markers 2/3/4 on a new Scintilla widget. */
void gitgutter_setup(GtkWidget *sci);

/*
 * Schedule a background git diff update for the given file path.
 * Calls are debounced: a pending update is cancelled if a new one arrives
 * within 800 ms. Safe to call from any GTK callback.
 */
void gitgutter_update(GtkWidget *sci, const char *path);

/* Remove all git gutter markers from the editor (e.g. when file is unsaved). */
void gitgutter_clear(GtkWidget *sci);

#endif /* GITGUTTER_H */
