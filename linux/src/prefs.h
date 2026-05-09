#ifndef PREFS_H
#define PREFS_H

#include <gtk/gtk.h>

/* ------------------------------------------------------------------ */
/* Auto-indent modes                                                   */
/* ------------------------------------------------------------------ */
#define AUTO_INDENT_NONE     0
#define AUTO_INDENT_BASIC    1
#define AUTO_INDENT_ADVANCED 2

/* ------------------------------------------------------------------ */
/* Persistent preferences                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    /* Editor */
    int      tab_width;               /* 1-16, default 4 */
    gboolean use_tabs;                /* FALSE = spaces (default) */
    int      auto_indent;             /* AUTO_INDENT_* */
    gboolean backspace_unindent;      /* backspace removes one indent stop */

    /* Display */
    gboolean highlight_current_line;  /* default TRUE */
    int      caret_width;             /* 1-3 px, default 1 */
    int      caret_blink_rate;        /* ms, 0 = no blink, default 600 */
    gboolean scroll_beyond_last_line; /* default FALSE */

    /* New Document */
    int      default_eol;             /* SC_EOL_LF/CRLF/CR, default SC_EOL_LF */
    char     default_encoding[32];    /* "UTF-8" etc, default "UTF-8" */

    /* General */
    gboolean show_full_path_in_title; /* default FALSE */
    gboolean copy_line_no_selection;  /* copy/cut whole line, default TRUE */

    /* Auto-completion */
    gboolean autocomplete_enabled;    /* default TRUE */
    int      autocomplete_min_chars;  /* trigger after N chars, default 1 */

    /* Auto-backup */
    gboolean backup_enabled;          /* default TRUE */
    int      backup_interval_secs;    /* seconds between backup writes, default 60 */
} NppPrefs;

extern NppPrefs g_prefs;

/* Load from ~/.config/notetux/config.xml (call before building UI) */
void prefs_load(void);

/* Save to ~/.config/notetux/config.xml */
void prefs_save(void);

/* Show (or raise) the Preferences dialog */
void prefs_dialog_show(GtkWidget *parent);

#endif /* PREFS_H */
