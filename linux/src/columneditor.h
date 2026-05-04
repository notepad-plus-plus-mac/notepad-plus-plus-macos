#ifndef COLUMNEDITOR_H
#define COLUMNEDITOR_H

#include <gtk/gtk.h>

/* Show (or raise) the Column Editor dialog.
 * Operates on the current selection in the active document.
 * If there is no selection, shows a notice and returns. */
void columneditor_show(GtkWidget *parent);

#endif /* COLUMNEDITOR_H */
