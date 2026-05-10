#ifndef CLIPHISTORY_H
#define CLIPHISTORY_H

#include <gtk/gtk.h>

/* Create the Clipboard History panel widget. Call once. */
GtkWidget *cliphistory_init(GtkWidget *window);

/* Show/hide the panel. */
void     cliphistory_set_visible(gboolean v);
gboolean cliphistory_is_visible(void);

#endif /* CLIPHISTORY_H */
