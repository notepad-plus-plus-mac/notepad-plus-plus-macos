#ifndef FUNCLIST_H
#define FUNCLIST_H

#include <gtk/gtk.h>

/* Create the panel widget. */
GtkWidget *funclist_init(void);

/* Immediately rebuild the list from the given Scintilla widget's text. */
void funclist_update(GtkWidget *sci);

/* Schedule a debounced rebuild (600 ms); call from SCN_MODIFIED. */
void funclist_schedule_update(GtkWidget *sci);

/* Show / hide the panel. */
void     funclist_set_visible(gboolean v);
gboolean funclist_is_visible(void);

#endif /* FUNCLIST_H */
