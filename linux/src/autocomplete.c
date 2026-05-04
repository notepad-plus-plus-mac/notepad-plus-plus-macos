/* autocomplete.c — word and keyword completion via SCI_AUTOCSHOW.
 *
 * On each SCN_CHARADDED with a word character, collect matching words from:
 *   1. Language keywords (from lexer_get_keywords)
 *   2. Words found in the current document (first 100 KB)
 * Merge into a sorted, deduplicated, space-separated list and call SCI_AUTOCSHOW.
 */
#include "autocomplete.h"
#include "lexer.h"
#include "prefs.h"
#include "sci_c.h"

#include <string.h>
#include <ctype.h>

#define AC_SCAN_LIMIT   (100 * 1024)  /* bytes of document to scan */
#define AC_MAX_WORDS    300           /* cap on list size */

static sptr_t sci_msg(GtkWidget *sci, unsigned int m, uptr_t w, sptr_t l)
{
    return scintilla_send_message(SCINTILLA(sci), m, w, l);
}

static gboolean is_word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* GTree comparator: case-insensitive sort, stable for different-case duplicates */
static gint cmp_ci(gconstpointer a, gconstpointer b, gpointer d)
{
    (void)d;
    int r = g_ascii_strcasecmp((const char *)a, (const char *)b);
    if (r != 0) return r;
    return strcmp((const char *)a, (const char *)b);
}

static gboolean collect_word(gpointer key, gpointer val, gpointer data)
{
    (void)val;
    GString *s = (GString *)data;
    if (s->len > 0) g_string_append_c(s, ' ');
    g_string_append(s, (const char *)key);
    return FALSE;
}

void autocomplete_setup(GtkWidget *sci)
{
    sci_msg(sci, SCI_AUTOCSETAUTOHIDE,       TRUE, 0);
    sci_msg(sci, SCI_AUTOCSETDROPRESTOFWORD, FALSE, 0);
    sci_msg(sci, SCI_AUTOCSETMAXHEIGHT,      8,    0);
    sci_msg(sci, SCI_AUTOCSETIGNORECASE,     TRUE, 0);
}

void autocomplete_on_char_added(GtkWidget *sci, int ch)
{
    if (!g_prefs.autocomplete_enabled) return;
    if (!is_word_char((char)ch)) {
        /* Non-word char: cancel any active autocomplete */
        if (sci_msg(sci, SCI_AUTOCACTIVE, 0, 0))
            sci_msg(sci, SCI_AUTOCCANCEL, 0, 0);
        return;
    }

    /* Measure prefix: word chars immediately before cursor */
    Sci_Position pos = (Sci_Position)sci_msg(sci, SCI_GETCURRENTPOS, 0, 0);
    Sci_Position word_start = pos;
    while (word_start > 0) {
        char c = (char)sci_msg(sci, SCI_GETCHARAT, (uptr_t)(word_start - 1), 0);
        if (!is_word_char(c)) break;
        word_start--;
    }
    int prefix_len = (int)(pos - word_start);
    if (prefix_len < g_prefs.autocomplete_min_chars) {
        if (sci_msg(sci, SCI_AUTOCACTIVE, 0, 0))
            sci_msg(sci, SCI_AUTOCCANCEL, 0, 0);
        return;
    }

    /* Extract prefix string */
    char prefix[64];
    if (prefix_len >= (int)sizeof(prefix)) return;
    for (int i = 0; i < prefix_len; i++)
        prefix[i] = (char)sci_msg(sci, SCI_GETCHARAT, (uptr_t)(word_start + i), 0);
    prefix[prefix_len] = '\0';

    /* GTree: sorted (case-insensitive), unique words matching prefix */
    GTree *words = g_tree_new_full(cmp_ci, NULL, g_free, NULL);

    /* 1. Language keywords */
    const char *lang = (const char *)g_object_get_data(G_OBJECT(sci), "npp-lang");
    const char *kw   = lexer_get_keywords(lang);
    if (kw) {
        gchar **parts = g_strsplit(kw, " ", -1);
        for (int i = 0; parts[i]; i++) {
            const char *w = parts[i];
            if (!*w) continue;
            if (g_ascii_strncasecmp(w, prefix, (gsize)prefix_len) == 0)
                g_tree_insert(words, g_strdup(w), GINT_TO_POINTER(1));
        }
        g_strfreev(parts);
    }

    /* 2. Document words */
    sptr_t doc_len  = sci_msg(sci, SCI_GETLENGTH, 0, 0);
    sptr_t scan_len = doc_len < AC_SCAN_LIMIT ? doc_len : AC_SCAN_LIMIT;
    if (scan_len > 0) {
        char *text = g_malloc((gsize)scan_len + 1);
        Sci_TextRangeFull tr;
        tr.chrg.cpMin = 0;
        tr.chrg.cpMax = scan_len;
        tr.lpstrText  = text;
        sci_msg(sci, SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
        text[scan_len] = '\0';

        sptr_t i = 0;
        while (i < scan_len && g_tree_nnodes(words) < AC_MAX_WORDS) {
            while (i < scan_len && !is_word_char(text[i])) i++;
            sptr_t ws = i;
            while (i < scan_len && is_word_char(text[i])) i++;
            int wlen = (int)(i - ws);
            if (wlen > prefix_len &&
                g_ascii_strncasecmp(text + ws, prefix, (gsize)prefix_len) == 0) {
                /* Skip the exact region already typed (cursor is in this word) */
                if (ws <= word_start && (ws + wlen) >= pos) continue;
                g_tree_insert(words, g_strndup(text + ws, (gsize)wlen),
                              GINT_TO_POINTER(1));
            }
        }
        g_free(text);
    }

    if (g_tree_nnodes(words) == 0) {
        g_tree_destroy(words);
        if (sci_msg(sci, SCI_AUTOCACTIVE, 0, 0))
            sci_msg(sci, SCI_AUTOCCANCEL, 0, 0);
        return;
    }

    GString *list = g_string_new(NULL);
    g_tree_foreach(words, collect_word, list);
    g_tree_destroy(words);

    sci_msg(sci, SCI_AUTOCSHOW, (uptr_t)prefix_len, (sptr_t)list->str);
    g_string_free(list, TRUE);
}
