#ifndef UDL_H
#define UDL_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UDL_KWLIST_COUNT 28

/* Load all UDL definitions from RESOURCES_DIR/userDefineLangs/ and
 * ~/.config/notetux/userDefineLangs/. Safe to call multiple times (no-op after first). */
void udl_load_all(void);

/* Return number of loaded UDL definitions. */
int  udl_count(void);

/* Return the display name of UDL at index i. */
const char *udl_name(int i);

/* Return a stable "udl:NAME" key for UDL at index i (valid until next reload). */
const char *udl_key(int i);

/* Find by display name — return index or -1. */
int udl_find_by_name(const char *name);

/* Find by file extension (no dot), case-insensitive — return index or -1. */
int udl_find_by_ext(const char *ext);

/* Apply UDL at index i to the Scintilla widget.
 * Assumes stylestore_apply_default / SCI_STYLECLEARALL / stylestore_apply_global
 * have NOT yet been called — udl_apply handles the full style pipeline. */
void udl_apply(GtkWidget *sci, int index);

#ifdef __cplusplus
}
#endif

#endif /* UDL_H */
