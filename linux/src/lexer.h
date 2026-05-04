#ifndef LEXER_H
#define LEXER_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Detect language from file extension and apply lexer + keywords to sci widget */
void lexer_apply_from_path(GtkWidget *sci, const char *path);

/* Apply a named language (pass NULL or "" for plain text) */
void lexer_apply(GtkWidget *sci, const char *lang_name);

/* Human-readable display name for a language (returns "Normal Text" if unknown/NULL) */
const char *lexer_display_name(const char *lang_name);

/* Keyword string for a language (space-separated), or NULL if none defined.
   Applies aliases: c/objc → cpp, typescript → javascript. */
const char *lexer_get_keywords(const char *lang_name);

#ifdef __cplusplus
}
#endif

#endif /* LEXER_H */
