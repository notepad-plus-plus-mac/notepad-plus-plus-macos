#ifndef DOCMAP_H
#define DOCMAP_H

#include <gtk/gtk.h>

/* Create the panel widget. */
GtkWidget *docmap_init(void);

/* Called on tab switch: share document and apply styles to the minimap. */
void docmap_update(GtkWidget *sci);

/* Called from SCN_UPDATEUI: sync scroll position and redraw viewport rect. */
void docmap_sync_scroll(GtkWidget *sci);

/* Show / hide the panel. */
void     docmap_set_visible(gboolean v);
gboolean docmap_is_visible(void);

#endif /* DOCMAP_H */
