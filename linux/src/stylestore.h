#ifndef STYLESTORE_H
#define STYLESTORE_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Public style entry — mirrors NPPStyleEntry                         */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[80];      /* "Default Style", "COMMENT", etc. */
    int  style_id;      /* Scintilla style index             */
    int  fg;            /* BGR, -1 = not set                 */
    int  bg;            /* BGR, -1 = not set                 */
    int  bold;          /* 0/1, -1 = not set                 */
    int  italic;        /* 0/1, -1 = not set                 */
    int  underline;     /* 0/1, -1 = not set                 */
    char font_name[80]; /* "" = inherit                      */
    int  font_size;     /* 0 = inherit                       */
} NppStyleEntry;

/* ------------------------------------------------------------------ */
/* Init / load                                                         */
/* ------------------------------------------------------------------ */

/* Load styles from stylers.model.xml. Call once at startup.
 * Falls back to RESOURCES_DIR/stylers.model.xml if xml_path is NULL. */
void stylestore_init(const char *xml_path);

/* Reload from a theme XML file (replaces current styles entirely).
 * path may be absolute; NULL reloads from the original model. */
void stylestore_load_theme(const char *path);

/* Write current styles to $HOME/.config/notetux/stylers.xml. */
void stylestore_save_user(void);

/* ------------------------------------------------------------------ */
/* Apply to Scintilla                                                  */
/* ------------------------------------------------------------------ */

/* Set STYLE_DEFAULT from the "Default Style" global entry.
 * Must be called BEFORE SCI_STYLECLEARALL so the values propagate. */
void stylestore_apply_default(GtkWidget *sci);

/* Apply global style overrides (line numbers, caret, selection, etc.).
 * Must be called AFTER SCI_STYLECLEARALL. */
void stylestore_apply_global(GtkWidget *sci);

/* Apply per-language colors for the Lexilla lexer name (e.g. "cpp").
 * Must be called AFTER SCI_STYLECLEARALL and after installing the lexer. */
void stylestore_apply_lexer(GtkWidget *sci, const char *lexer_id);

/* ------------------------------------------------------------------ */
/* Read / edit access (for the style editor dialog)                   */
/* ------------------------------------------------------------------ */

/* Number of lexer blocks (includes "global"). */
int stylestore_block_count(void);

/* ID string of block i (e.g. "global", "cpp", "python"). */
const char *stylestore_block_id(int block_idx);

/* Number of style entries in block i. */
int stylestore_entry_count(int block_idx);

/* Read entry j of block i.  Returns FALSE if out of range. */
gboolean stylestore_get_entry(int block_idx, int entry_idx,
                               NppStyleEntry *out);

/* Replace entry j of block i.  Returns FALSE if out of range. */
gboolean stylestore_set_entry(int block_idx, int entry_idx,
                               const NppStyleEntry *in);

#ifdef __cplusplus
}
#endif

#endif /* STYLESTORE_H */
