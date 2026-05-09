/* stylestore.c — Syntax highlighting style store for the Linux GTK3 port.
 * Ports NPPStyleStore / applyDefaultTheme / applyGlobalStyleColors /
 * applyLexerColors from StyleConfiguratorWindowController.mm + EditorView.mm.
 *
 * Parses stylers.model.xml (same XML the macOS port uses) with GLib's
 * GMarkupParser. Config override at $HOME/.config/notetux/stylers.xml.
 */
#include "stylestore.h"
#include "sci_c.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifndef RESOURCES_DIR
#define RESOURCES_DIR "../../resources"
#endif

/* ------------------------------------------------------------------ */
/* Internal data model (superset of public NppStyleEntry)             */
/* ------------------------------------------------------------------ */

typedef NppStyleEntry StyleEntry;  /* same layout */

typedef struct {
    char        id[64];     /* "cpp", "python", "global", etc. */
    StyleEntry *entries;
    int         count;
    int         cap;
} LexerBlock;

static LexerBlock *s_blocks      = NULL;
static int         s_block_count = 0;
static int         s_block_cap   = 0;
static gboolean    s_loaded      = FALSE;

/* ------------------------------------------------------------------ */
/* Color helpers — RRGGBB → Scintilla BGR (same as macOS sciColor())  */
/* ------------------------------------------------------------------ */

static int parse_rrggbb(const char *hex)
{
    if (!hex || !*hex) return -1;
    if (*hex == '#') hex++;
    size_t l = strlen(hex);
    if (l != 6) return -1;
    unsigned long v = strtoul(hex, NULL, 16);
    int r = (int)((v >> 16) & 0xFF);
    int g = (int)((v >>  8) & 0xFF);
    int b = (int)( v        & 0xFF);
    return r | (g << 8) | (b << 16);   /* Scintilla BGR / Windows COLORREF */
}

/* Scintilla BGR → RRGGBB string (for XML serialisation) */
static void bgr_to_rrggbb(int bgr, char out[7])
{
    int r = bgr & 0xFF;
    int g = (bgr >> 8)  & 0xFF;
    int b = (bgr >> 16) & 0xFF;
    snprintf(out, 7, "%02X%02X%02X", r, g, b);
}

/* ------------------------------------------------------------------ */
/* Block management                                                    */
/* ------------------------------------------------------------------ */

static void free_blocks(void)
{
    for (int i = 0; i < s_block_count; i++)
        g_free(s_blocks[i].entries);
    g_free(s_blocks);
    s_blocks      = NULL;
    s_block_count = 0;
    s_block_cap   = 0;
}

static LexerBlock *get_or_create_block(const char *id)
{
    for (int i = 0; i < s_block_count; i++)
        if (strcmp(s_blocks[i].id, id) == 0)
            return &s_blocks[i];

    if (s_block_count >= s_block_cap) {
        s_block_cap = s_block_cap ? s_block_cap * 2 : 64;
        s_blocks = g_realloc(s_blocks, (gsize)(s_block_cap * (int)sizeof(LexerBlock)));
    }
    LexerBlock *b = &s_blocks[s_block_count++];
    memset(b, 0, sizeof(*b));
    g_strlcpy(b->id, id, sizeof(b->id));
    return b;
}

static void block_add(LexerBlock *b, const StyleEntry *e)
{
    if (b->count >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 16;
        b->entries = g_realloc(b->entries, (gsize)(b->cap * (int)sizeof(StyleEntry)));
    }
    b->entries[b->count++] = *e;
}

/* Overwrite an existing entry by name, or append. */
static void block_upsert(LexerBlock *b, const StyleEntry *e)
{
    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->entries[i].name, e->name) == 0 &&
            b->entries[i].style_id == e->style_id) {
            b->entries[i] = *e;
            return;
        }
    }
    block_add(b, e);
}

/* ------------------------------------------------------------------ */
/* System monospace font detection                                     */
/* ------------------------------------------------------------------ */

/* Returns a newly-allocated string (caller must g_free).
 * Tries GSettings first; falls back to "Monospace 10" conventions. */
static char *detect_monospace_font(int *size_out)
{
    char *name = NULL;
    int   size = 10;

#if defined(HAVE_GSETTINGS) || !defined(NO_GSETTINGS)
    GSettingsSchemaSource *src =
        g_settings_schema_source_get_default();
    GSettingsSchema *schema = src ?
        g_settings_schema_source_lookup(src,
            "org.gnome.desktop.interface", TRUE) : NULL;
    if (schema) {
        g_settings_schema_unref(schema);
        GSettings *gs = g_settings_new("org.gnome.desktop.interface");
        gchar *val = g_settings_get_string(gs, "monospace-font-name");
        g_object_unref(gs);
        if (val && *val) {
            /* Format is "Font Name SIZE", e.g. "Monospace 10" */
            gchar *last_space = strrchr(val, ' ');
            if (last_space && *(last_space + 1)) {
                int sz = atoi(last_space + 1);
                if (sz > 4 && sz < 100) {
                    size = sz;
                    *last_space = '\0';
                }
            }
            name = g_strdup(val);
        }
        g_free(val);
    }
#endif

    if (!name || !*name) {
        g_free(name);
        name = g_strdup("Monospace");
    }
    if (size_out) *size_out = size;
    return name;
}

/* ------------------------------------------------------------------ */
/* GMarkupParser callbacks (SAX-style)                                */
/* ------------------------------------------------------------------ */

typedef struct {
    gboolean in_global;
    char     current_lexer[64];
} PCtx;

static const char *attr_val(const gchar **names, const gchar **vals,
                             const char *key)
{
    for (int i = 0; names[i]; i++)
        if (strcmp(names[i], key) == 0)
            return vals[i];
    return NULL;
}

static void on_start(GMarkupParseContext *ctx,
                     const gchar *el,
                     const gchar **names,
                     const gchar **vals,
                     gpointer ud,
                     GError **err)
{
    (void)ctx; (void)err;
    PCtx *pc = (PCtx *)ud;

    if (strcmp(el, "GlobalStyles") == 0) {
        pc->in_global = TRUE;
        g_strlcpy(pc->current_lexer, "global", sizeof(pc->current_lexer));
        return;
    }
    if (strcmp(el, "LexerType") == 0) {
        pc->in_global = FALSE;
        const char *n = attr_val(names, vals, "name");
        if (n)
            g_strlcpy(pc->current_lexer, n, sizeof(pc->current_lexer));
        else
            pc->current_lexer[0] = '\0';
        return;
    }

    gboolean is_widget = (strcmp(el, "WidgetStyle") == 0);
    gboolean is_words  = (strcmp(el, "WordsStyle")  == 0);

    if (!((is_widget && pc->in_global) || (is_words && !pc->in_global)))
        return;
    if (!pc->current_lexer[0]) return;

    StyleEntry e;
    memset(&e, 0, sizeof(e));
    e.fg = e.bg = e.bold = e.italic = e.underline = -1;
    e.font_size = 0;

    const char *v;
    if ((v = attr_val(names, vals, "name")))
        g_strlcpy(e.name, v, sizeof(e.name));
    if ((v = attr_val(names, vals, "styleID")))
        e.style_id = atoi(v);
    if ((v = attr_val(names, vals, "fgColor")) && strlen(v) == 6)
        e.fg = parse_rrggbb(v);
    if ((v = attr_val(names, vals, "bgColor")) && strlen(v) == 6)
        e.bg = parse_rrggbb(v);
    if ((v = attr_val(names, vals, "fontStyle"))) {
        int fs = atoi(v);
        e.bold      = (fs & 1) ? 1 : 0;
        e.italic    = (fs & 2) ? 1 : 0;
        e.underline = (fs & 4) ? 1 : 0;
    }
    if ((v = attr_val(names, vals, "fontName")) && *v)
        g_strlcpy(e.font_name, v, sizeof(e.font_name));
    if ((v = attr_val(names, vals, "fontSize")) && *v)
        e.font_size = atoi(v);

    LexerBlock *b = get_or_create_block(pc->current_lexer);
    block_upsert(b, &e);
}

static void on_end(GMarkupParseContext *ctx,
                   const gchar *el,
                   gpointer ud,
                   GError **err)
{
    (void)ctx; (void)err;
    PCtx *pc = (PCtx *)ud;
    if (strcmp(el, "GlobalStyles") == 0) pc->in_global = FALSE;
}

/* Escape bare & characters that are not part of a valid XML entity.
   NPP theme files sometimes contain unescaped & in attribute values. */
static gchar *fix_bare_ampersands(const gchar *src, gsize len)
{
    GString *out = g_string_sized_new(len + 64);
    for (gsize i = 0; i < len; i++) {
        if (src[i] != '&') { g_string_append_c(out, src[i]); continue; }
        /* Look ahead: valid entity = &#…; or &name; */
        gboolean valid = FALSE;
        if (i + 1 < len) {
            if (src[i+1] == '#') {
                valid = TRUE;
            } else {
                gsize j = i + 1;
                while (j < len && j < i + 20 && (g_ascii_isalnum(src[j]) || src[j] == '_'))
                    j++;
                if (j < len && src[j] == ';' && j > i + 1)
                    valid = TRUE;
            }
        }
        g_string_append(out, valid ? "&" : "&amp;");
    }
    return g_string_free(out, FALSE);
}

/* Parse a single XML file, merging into s_blocks. */
static void parse_file(const char *path)
{
    gchar  *contents = NULL;
    gsize   len      = 0;
    GError *err      = NULL;

    if (!g_file_get_contents(path, &contents, &len, &err)) {
        if (err) { g_warning("stylestore: %s", err->message); g_error_free(err); }
        return;
    }

    gchar *fixed = fix_bare_ampersands(contents, len);
    g_free(contents);
    gsize fixed_len = strlen(fixed);

    GMarkupParser parser = { on_start, on_end, NULL, NULL, NULL };
    PCtx pc = { FALSE, "" };
    GMarkupParseContext *ctx =
        g_markup_parse_context_new(&parser, G_MARKUP_DEFAULT_FLAGS, &pc, NULL);

    err = NULL;
    if (!g_markup_parse_context_parse(ctx, fixed, (gssize)fixed_len, &err))
        g_warning("stylestore: parse error in %s: %s", path,
                  err ? err->message : "?");
    if (err) g_error_free(err);
    g_markup_parse_context_free(ctx);
    g_free(fixed);
}

/* ------------------------------------------------------------------ */
/* Replace "Courier New" in Default Style with the system font        */
/* ------------------------------------------------------------------ */

static void fix_default_font(void)
{
    for (int i = 0; i < s_block_count; i++) {
        if (strcmp(s_blocks[i].id, "global") != 0) continue;
        LexerBlock *b = &s_blocks[i];
        for (int j = 0; j < b->count; j++) {
            StyleEntry *e = &b->entries[j];
            if (strcmp(e->name, "Default Style") != 0) continue;
            /* Only replace Windows-specific fonts not available on Linux */
            if (strcmp(e->font_name, "Courier New") == 0 ||
                strcmp(e->font_name, "Lucida Console") == 0 ||
                e->font_name[0] == '\0') {
                int sz = e->font_size > 0 ? e->font_size : 0;
                int detected_sz = 10;
                char *font = detect_monospace_font(&detected_sz);
                g_strlcpy(e->font_name, font, sizeof(e->font_name));
                g_free(font);
                if (sz == 0) e->font_size = detected_sz;
            }
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public: init / load / save                                         */
/* ------------------------------------------------------------------ */

void stylestore_init(const char *xml_path)
{
    if (s_loaded) return;
    s_loaded = TRUE;

    char model_path[512];
    if (xml_path)
        g_strlcpy(model_path, xml_path, sizeof(model_path));
    else
        snprintf(model_path, sizeof(model_path),
                 RESOURCES_DIR "/stylers.model.xml");

    parse_file(model_path);
    fix_default_font();

    /* Overlay user overrides from $HOME/.config/notetux/stylers.xml */
    const char *home = g_get_home_dir();
    if (home) {
        char user_path[512];
        snprintf(user_path, sizeof(user_path),
                 "%s/.config/notetux/stylers.xml", home);
        if (g_file_test(user_path, G_FILE_TEST_EXISTS))
            parse_file(user_path);
    }
}

void stylestore_load_theme(const char *path)
{
    free_blocks();
    s_loaded = FALSE;

    if (path && *path) {
        /* Load theme file as the sole source */
        parse_file(path);
        fix_default_font();
        s_loaded = TRUE;
    } else {
        /* Reload default model */
        stylestore_init(NULL);
    }
}

void stylestore_save_user(void)
{
    const char *home = g_get_home_dir();
    if (!home) return;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config/notetux", home);
    g_mkdir_with_parents(dir, 0755);

    char path[512];
    snprintf(path, sizeof(path), "%s/stylers.xml", dir);

    FILE *f = fopen(path, "w");
    if (!f) { g_warning("stylestore: cannot write %s", path); return; }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");
    fprintf(f, "<NotepadPlus>\n");

    /* Global styles */
    fprintf(f, "    <GlobalStyles>\n");
    for (int i = 0; i < s_block_count; i++) {
        if (strcmp(s_blocks[i].id, "global") != 0) continue;
        LexerBlock *b = &s_blocks[i];
        for (int j = 0; j < b->count; j++) {
            StyleEntry *e = &b->entries[j];
            char fg_s[8] = "", bg_s[8] = "";
            char fsz_s[8] = "";
            if (e->fg >= 0) bgr_to_rrggbb(e->fg, fg_s);
            if (e->bg >= 0) bgr_to_rrggbb(e->bg, bg_s);
            if (e->font_size > 0) snprintf(fsz_s, sizeof(fsz_s), "%d", e->font_size);
            int fs = 0;
            if (e->bold > 0) fs |= 1;
            if (e->italic > 0) fs |= 2;
            if (e->underline > 0) fs |= 4;
            fprintf(f, "        <WidgetStyle name=\"%s\" styleID=\"%d\""
                    " fgColor=\"%s\" bgColor=\"%s\""
                    " fontName=\"%s\" fontSize=\"%s\""
                    " fontStyle=\"%d\" />\n",
                    e->name, e->style_id,
                    fg_s, bg_s,
                    e->font_name,
                    fsz_s,
                    fs);
        }
        break;
    }
    fprintf(f, "    </GlobalStyles>\n");

    /* Lexer styles */
    fprintf(f, "    <LexerStyles>\n");
    for (int i = 0; i < s_block_count; i++) {
        if (strcmp(s_blocks[i].id, "global") == 0) continue;
        LexerBlock *b = &s_blocks[i];
        fprintf(f, "        <LexerType name=\"%s\" desc=\"\" excluded=\"no\">\n",
                b->id);
        for (int j = 0; j < b->count; j++) {
            StyleEntry *e = &b->entries[j];
            char fg_s[8] = "", bg_s[8] = "";
            if (e->fg >= 0) bgr_to_rrggbb(e->fg, fg_s);
            if (e->bg >= 0) bgr_to_rrggbb(e->bg, bg_s);
            int fs = 0;
            if (e->bold > 0) fs |= 1;
            if (e->italic > 0) fs |= 2;
            if (e->underline > 0) fs |= 4;
            fprintf(f, "            <WordsStyle name=\"%s\" styleID=\"%d\""
                    " fgColor=\"%s\" bgColor=\"%s\""
                    " fontName=\"%s\" fontSize=\"%d\""
                    " fontStyle=\"%d\" />\n",
                    e->name, e->style_id,
                    fg_s, bg_s,
                    e->font_name,
                    e->font_size,
                    fs);
        }
        fprintf(f, "        </LexerType>\n");
    }
    fprintf(f, "    </LexerStyles>\n");
    fprintf(f, "</NotepadPlus>\n");
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* SCI helper                                                          */
/* ------------------------------------------------------------------ */

static sptr_t sci_msg(GtkWidget *sci, unsigned int m, uptr_t w, sptr_t l)
{
    return scintilla_send_message(SCINTILLA(sci), m, w, l);
}

static void apply_entry(GtkWidget *sci, int sid, const StyleEntry *e)
{
    if (e->fg >= 0) sci_msg(sci, SCI_STYLESETFORE, (uptr_t)sid, e->fg);
    if (e->bg >= 0) sci_msg(sci, SCI_STYLESETBACK, (uptr_t)sid, e->bg);
    if (e->bold      >= 0) sci_msg(sci, SCI_STYLESETBOLD,      (uptr_t)sid, e->bold);
    if (e->italic    >= 0) sci_msg(sci, SCI_STYLESETITALIC,    (uptr_t)sid, e->italic);
    if (e->underline >= 0) sci_msg(sci, SCI_STYLESETUNDERLINE, (uptr_t)sid, e->underline);
    if (e->font_name[0])
        sci_msg(sci, SCI_STYLESETFONT, (uptr_t)sid, (sptr_t)e->font_name);
    if (e->font_size > 0)
        sci_msg(sci, SCI_STYLESETSIZE, (uptr_t)sid, e->font_size);
}

static const StyleEntry *find_global(const char *name)
{
    for (int i = 0; i < s_block_count; i++) {
        if (strcmp(s_blocks[i].id, "global") != 0) continue;
        LexerBlock *b = &s_blocks[i];
        for (int j = 0; j < b->count; j++)
            if (strcmp(b->entries[j].name, name) == 0)
                return &b->entries[j];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public: apply functions                                             */
/* ------------------------------------------------------------------ */

void stylestore_apply_default(GtkWidget *sci)
{
    const StyleEntry *def = find_global("Default Style");
    if (!def) return;
    apply_entry(sci, STYLE_DEFAULT, def);
    if (def->fg >= 0)
        sci_msg(sci, SCI_SETCARETFORE, (uptr_t)def->fg, 0);
}

void stylestore_apply_global(GtkWidget *sci)
{
    const StyleEntry *ln = find_global("Line number margin");
    if (ln) apply_entry(sci, STYLE_LINENUMBER, ln);

    const StyleEntry *ig = find_global("Indent guideline style");
    if (ig) apply_entry(sci, 37, ig);

    const StyleEntry *bh = find_global("Brace highlight style");
    if (bh) apply_entry(sci, STYLE_BRACELIGHT, bh);

    const StyleEntry *bb = find_global("Bad brace colour");
    if (bb) apply_entry(sci, STYLE_BRACEBAD, bb);

    const StyleEntry *cc = find_global("Caret colour");
    if (cc && cc->fg >= 0)
        sci_msg(sci, SCI_SETCARETFORE, (uptr_t)cc->fg, 0);

    const StyleEntry *cl = find_global("Current line background colour");
    if (cl && cl->bg >= 0) {
        sci_msg(sci, SCI_SETCARETLINEVISIBLE, 1, 0);
        sci_msg(sci, SCI_SETCARETLINEBACK,    (uptr_t)cl->bg, 0);
    }

    const StyleEntry *sel = find_global("Selected text colour");
    if (sel && sel->bg >= 0)
        sci_msg(sci, SCI_SETSELBACK, 1, sel->bg);

    const StyleEntry *ws = find_global("White space symbol");
    if (ws && ws->fg >= 0)
        sci_msg(sci, SCI_SETWHITESPACEFORE, 1, ws->fg);

    const StyleEntry *fm = find_global("Fold margin");
    int fmbg = (fm && fm->bg >= 0) ? fm->bg : 0xE9E9E9;
    sci_msg(sci, SCI_SETFOLDMARGINCOLOUR,   1, fmbg);
    sci_msg(sci, SCI_SETFOLDMARGINHICOLOUR, 1, fmbg);

    const StyleEntry *fold = find_global("Fold");
    int fold_fg = (fold && fold->fg >= 0) ? fold->fg : 0x808080;
    int fold_bg = (fold && fold->bg >= 0) ? fold->bg : 0xF3F3F3;
    for (int mn = SC_MARKNUM_FOLDER; mn <= SC_MARKNUM_FOLDEROPEN; mn++) {
        sci_msg(sci, SCI_MARKERSETFORE, (uptr_t)mn, fold_fg);
        sci_msg(sci, SCI_MARKERSETBACK, (uptr_t)mn, fold_bg);
    }
    sci_msg(sci, SCI_MARKERENABLEHIGHLIGHT, 1, 0);
}

void stylestore_apply_lexer(GtkWidget *sci, const char *lexer_id)
{
    if (!s_loaded || !lexer_id || !*lexer_id) return;

    char lid[64];
    g_strlcpy(lid, lexer_id, sizeof(lid));
    for (int i = 0; lid[i]; i++) lid[i] = (char)tolower((unsigned char)lid[i]);

    LexerBlock *b = NULL;
    for (int i = 0; i < s_block_count; i++) {
        if (strcmp(s_blocks[i].id, lid) == 0) { b = &s_blocks[i]; break; }
    }
    if (!b || !b->count) return;

    for (int j = 0; j < b->count; j++)
        apply_entry(sci, b->entries[j].style_id, &b->entries[j]);
}

/* ------------------------------------------------------------------ */
/* Public: read / edit access                                         */
/* ------------------------------------------------------------------ */

int stylestore_block_count(void)
{
    return s_block_count;
}

const char *stylestore_block_id(int idx)
{
    if (idx < 0 || idx >= s_block_count) return NULL;
    return s_blocks[idx].id;
}

int stylestore_entry_count(int block_idx)
{
    if (block_idx < 0 || block_idx >= s_block_count) return 0;
    return s_blocks[block_idx].count;
}

gboolean stylestore_get_entry(int block_idx, int entry_idx,
                               NppStyleEntry *out)
{
    if (block_idx < 0 || block_idx >= s_block_count) return FALSE;
    LexerBlock *b = &s_blocks[block_idx];
    if (entry_idx < 0 || entry_idx >= b->count) return FALSE;
    *out = b->entries[entry_idx];
    return TRUE;
}

gboolean stylestore_set_entry(int block_idx, int entry_idx,
                               const NppStyleEntry *in)
{
    if (block_idx < 0 || block_idx >= s_block_count) return FALSE;
    LexerBlock *b = &s_blocks[block_idx];
    if (entry_idx < 0 || entry_idx >= b->count) return FALSE;
    b->entries[entry_idx] = *in;
    return TRUE;
}
