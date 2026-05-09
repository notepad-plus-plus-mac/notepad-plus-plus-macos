#ifndef SHORTCUTMAP_H
#define SHORTCUTMAP_H

#include <gtk/gtk.h>

typedef struct {
    const char     *id;
    const char     *label;
    const char     *category;
    guint           default_key;
    GdkModifierType default_mod;
    guint           current_key;   /* 0 = use default */
    GdkModifierType current_mod;
    GtkWidget      *widget;        /* menu item (set during build) */
    GtkAccelGroup  *group;
} ShortcutEntry;

/* Returns the global table; *count receives its size */
ShortcutEntry *shortcut_table(int *count);

/* Find by ID; NULL if not found */
ShortcutEntry *shortcut_find(const char *id);

/* Called during menu build: stores widget + group ref for live rebinding */
void shortcut_register(const char *id, GtkWidget *widget, GtkAccelGroup *group);

/* Load overrides from ~/.config/notetux/shortcuts.xml */
void shortcut_load(void);

/* Write current bindings to ~/.config/notetux/shortcuts.xml */
void shortcut_save(void);

/* Show (or re-raise) the shortcut mapper dialog */
void shortcut_mapper_show(GtkWidget *parent);

#endif /* SHORTCUTMAP_H */
