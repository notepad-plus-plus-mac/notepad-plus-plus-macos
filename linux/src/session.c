/* session.c — Session save / restore for the Linux GTK3 port.
 *
 * Persists open file paths + scroll/caret positions to
 * ~/.config/notetux/session.xml in NPP-compatible XML format.
 *
 * Format:
 *   <NotepadPlus>
 *     <Session activeIndex="N">
 *       <mainView activeIndex="N">
 *         <File filename="…" firstVisibleLine="N" xOffset="N"
 *               caretPosition="N" encoding="UTF-8" />
 *         …
 *       </mainView>
 *     </Session>
 *   </NotepadPlus>
 *
 * Only tabs with a saved filepath are persisted; unsaved "new N" docs
 * are intentionally skipped.
 */
#include "session.h"
#include "editor.h"
#include "sci_c.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Path helper                                                         */
/* ------------------------------------------------------------------ */

static const char *session_path(void)
{
    static char s_path[512];
    if (!s_path[0])
        snprintf(s_path, sizeof(s_path), "%s/notetux/session.xml",
                 g_get_user_config_dir());
    return s_path;
}

/* ------------------------------------------------------------------ */
/* Save                                                                */
/* ------------------------------------------------------------------ */

void session_save(void)
{
    int total  = editor_page_count();
    int active = editor_current_page();

    /* Build the list of pages that have a filepath (saved files only) */
    int  active_saved = 0;  /* index into saved-files list for the active tab */
    int  saved_count  = 0;

    GString *xml = g_string_new(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
        "<NotepadPlus>\n");

    /* First pass: determine active_saved index */
    for (int i = 0; i < active; i++) {
        NppDoc *d = editor_doc_at(i);
        if (d && d->filepath) saved_count++;
    }
    active_saved = saved_count;  /* index of active tab in saved list */

    /* Count total saved */
    saved_count = 0;
    for (int i = 0; i < total; i++) {
        NppDoc *d = editor_doc_at(i);
        if (d && d->filepath) saved_count++;
    }

    g_string_append_printf(xml,
        "\t<Session activeIndex=\"%d\">\n"
        "\t\t<mainView activeIndex=\"0\">\n",
        active_saved);

    for (int i = 0; i < total; i++) {
        NppDoc *doc = editor_doc_at(i);
        if (!doc || !doc->filepath) continue;

        sptr_t caret      = scintilla_send_message(SCINTILLA(doc->sci),
                                SCI_GETCURRENTPOS, 0, 0);
        sptr_t first_line = scintilla_send_message(SCINTILLA(doc->sci),
                                SCI_GETFIRSTVISIBLELINE, 0, 0);
        sptr_t xoffset    = scintilla_send_message(SCINTILLA(doc->sci),
                                SCI_GETXOFFSET, 0, 0);

        gchar *escaped = g_markup_escape_text(doc->filepath, -1);
        g_string_append_printf(xml,
            "\t\t\t<File filename=\"%s\""
            " firstVisibleLine=\"%ld\""
            " xOffset=\"%ld\""
            " caretPosition=\"%ld\""
            " encoding=\"%s\" />\n",
            escaped,
            (long)first_line,
            (long)xoffset,
            (long)caret,
            doc->encoding ? doc->encoding : "UTF-8");
        g_free(escaped);
    }

    g_string_append(xml,
        "\t\t</mainView>\n"
        "\t</Session>\n"
        "</NotepadPlus>\n");

    /* Ensure config dir exists */
    gchar *dir = g_build_filename(g_get_user_config_dir(), "notetux", NULL);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    GError *err = NULL;
    if (!g_file_set_contents(session_path(), xml->str, -1, &err)) {
        g_warning("session_save: %s", err->message);
        g_error_free(err);
    }
    g_string_free(xml, TRUE);
}

/* ------------------------------------------------------------------ */
/* Restore — XML parser                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char  filepath[1024];
    long  first_line;
    long  xoffset;
    long  caret_pos;
    char  encoding[32];
} SessionEntry;

typedef struct {
    int           active_index;
    SessionEntry *entries;
    int           count;
    int           cap;
} ParseState;

static void xml_start(GMarkupParseContext *ctx, const gchar *el,
                      const gchar **names, const gchar **vals,
                      gpointer ud, GError **err)
{
    (void)ctx; (void)err;
    ParseState *st = (ParseState *)ud;

    if (strcmp(el, "Session") == 0) {
        for (int i = 0; names[i]; i++)
            if (strcmp(names[i], "activeIndex") == 0)
                st->active_index = atoi(vals[i]);
        return;
    }

    if (strcmp(el, "File") != 0) return;

    /* Grow array if needed */
    if (st->count >= st->cap) {
        st->cap = st->cap ? st->cap * 2 : 8;
        st->entries = g_realloc(st->entries,
                                (gsize)st->cap * sizeof(SessionEntry));
    }

    SessionEntry *e = &st->entries[st->count];
    memset(e, 0, sizeof(*e));
    snprintf(e->encoding, sizeof(e->encoding), "UTF-8");

    for (int i = 0; names[i]; i++) {
        if      (strcmp(names[i], "filename")         == 0)
            snprintf(e->filepath, sizeof(e->filepath), "%s", vals[i]);
        else if (strcmp(names[i], "firstVisibleLine") == 0)
            e->first_line = atol(vals[i]);
        else if (strcmp(names[i], "xOffset")          == 0)
            e->xoffset    = atol(vals[i]);
        else if (strcmp(names[i], "caretPosition")    == 0)
            e->caret_pos  = atol(vals[i]);
        else if (strcmp(names[i], "encoding")         == 0)
            snprintf(e->encoding, sizeof(e->encoding), "%s", vals[i]);
    }

    if (e->filepath[0]) st->count++;
}

static GMarkupParser s_parser = { xml_start, NULL, NULL, NULL, NULL };

void session_restore(void)
{
    gchar *xml = NULL;
    if (!g_file_get_contents(session_path(), &xml, NULL, NULL))
        return;  /* no session file yet — first run */

    ParseState st = { 0, NULL, 0, 0 };

    GMarkupParseContext *ctx = g_markup_parse_context_new(&s_parser, 0, &st, NULL);
    g_markup_parse_context_parse(ctx, xml, -1, NULL);
    g_markup_parse_context_free(ctx);
    g_free(xml);

    if (st.count == 0) {
        g_free(st.entries);
        return;
    }

    /* Open each file; skip those that no longer exist */
    int restored = 0;
    int last_page = -1;

    for (int i = 0; i < st.count; i++) {
        SessionEntry *e = &st.entries[i];
        if (!g_file_test(e->filepath, G_FILE_TEST_EXISTS)) continue;

        if (!editor_open_path(e->filepath)) continue;

        /* Restore scroll and caret */
        NppDoc *doc = editor_current_doc();
        if (doc) {
            scintilla_send_message(SCINTILLA(doc->sci),
                SCI_SETFIRSTVISIBLELINE, (uptr_t)e->first_line, 0);
            scintilla_send_message(SCINTILLA(doc->sci),
                SCI_SETXOFFSET, (uptr_t)e->xoffset, 0);
            scintilla_send_message(SCINTILLA(doc->sci),
                SCI_GOTOPOS, (uptr_t)e->caret_pos, 0);
            scintilla_send_message(SCINTILLA(doc->sci),
                SCI_SCROLLCARET, 0, 0);

            if (doc->encoding)
                g_free(doc->encoding);
            doc->encoding = g_strdup(e->encoding);
        }

        if (restored == st.active_index)
            last_page = editor_current_page();
        restored++;
    }

    /* Switch to the tab that was active when the session was saved */
    if (last_page >= 0) {
        GtkWidget *nb = editor_get_notebook();
        gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), last_page);
    }

    g_free(st.entries);
}
