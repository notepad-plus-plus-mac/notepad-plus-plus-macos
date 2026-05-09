#ifndef MACRO_H
#define MACRO_H

#include <gtk/gtk.h>
#include "sci_c.h"

/* Start/stop Scintilla macro recording. */
void macro_start_recording(GtkWidget *sci);
void macro_stop_recording(GtkWidget *sci);

/* Called from SCN_MACRORECORD: store one recorded step. */
void macro_on_record(unsigned int msg, uptr_t wp, sptr_t lp);

/* Play back the stored macro once, or n times (prompts for n). */
void macro_playback(GtkWidget *sci);
void macro_playback_n(GtkWidget *sci, GtkWindow *parent);

/* State queries */
gboolean macro_is_recording(void);
gboolean macro_has_macro(void);

#endif /* MACRO_H */
