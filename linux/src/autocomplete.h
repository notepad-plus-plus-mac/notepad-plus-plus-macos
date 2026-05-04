#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

#include <gtk/gtk.h>

/* Call once per sci widget after setup (sets Scintilla autocomplete options). */
void autocomplete_setup(GtkWidget *sci);

/* Call from on_sci_notify when SCN_CHARADDED fires. */
void autocomplete_on_char_added(GtkWidget *sci, int ch);

#endif /* AUTOCOMPLETE_H */
