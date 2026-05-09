#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <gtk/gtk.h>

/* Create the panel widget; parent_window used for the open-folder dialog. */
GtkWidget *workspace_init(GtkWidget *parent_window);

/* Load a directory as the workspace root, replacing any previous tree. */
void workspace_set_folder(const char *path);

/* Show / hide the panel. */
void     workspace_set_visible(gboolean v);
gboolean workspace_is_visible(void);

#endif /* WORKSPACE_H */
