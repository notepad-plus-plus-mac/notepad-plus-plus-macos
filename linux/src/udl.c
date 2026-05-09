/* udl.c — User Defined Language (UDL) manager for the Linux GTK3 port.
 * Parses ~/.config/notetux/userDefineLangs/*.xml and RESOURCES_DIR/userDefineLangs/*.xml,
 * then applies the "user" Lexilla lexer with the proper properties and keyword lists.
 */
#include "udl.h"
#include "sci_c.h"
#include "stylestore.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Bridge to C++ CreateLexer() */
extern void *lexilla_create_lexer(const char *name);

/* ------------------------------------------------------------------ */
/* SCE_USER_STYLE constants (mirrors lexilla/include/SciLexer.h)      */
/* ------------------------------------------------------------------ */
#define UDL_STYLE_DEFAULT           0
#define UDL_STYLE_COMMENT           1
#define UDL_STYLE_COMMENTLINE       2
#define UDL_STYLE_NUMBER            3
#define UDL_STYLE_KEYWORD1          4
#define UDL_STYLE_KEYWORD2          5
#define UDL_STYLE_KEYWORD3          6
#define UDL_STYLE_KEYWORD4          7
#define UDL_STYLE_KEYWORD5          8
#define UDL_STYLE_KEYWORD6          9
#define UDL_STYLE_KEYWORD7          10
#define UDL_STYLE_KEYWORD8          11
#define UDL_STYLE_OPERATOR          12
#define UDL_STYLE_FOLDER_IN_CODE1   13
#define UDL_STYLE_FOLDER_IN_CODE2   14
#define UDL_STYLE_FOLDER_IN_COMMENT 15
#define UDL_STYLE_DELIMITER1        16
#define UDL_STYLE_DELIMITER2        17
#define UDL_STYLE_DELIMITER3        18
#define UDL_STYLE_DELIMITER4        19
#define UDL_STYLE_DELIMITER5        20
#define UDL_STYLE_DELIMITER6        21
#define UDL_STYLE_DELIMITER7        22
#define UDL_STYLE_DELIMITER8        23
#define UDL_STYLE_TOTAL             24

/* ------------------------------------------------------------------ */
/* Internal types                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int  fg;           /* Scintilla BGR, -1 = not set   */
    int  bg;           /* Scintilla BGR, -1 = not set   */
    int  color_style;  /* 0=default colors, 1=custom     */
    int  font_style;   /* bitmask: 1=bold,2=italic,4=ul  */
    int  font_size;    /* 0=inherit                       */
    int  nesting;      /* nesting bitmask (→ SCI_SETPROPERTY) */
    char font_name[64];
} UdlStyleEntry;

typedef struct {
    char          name[128];
    char          ext[512];    /* space-separated file extensions */
    char          key[136];    /* "udl:" + name, stable pointer    */
    /* Settings/Global */
    int           case_ignored;
    int           allow_fold_comments;
    int           fold_compact;
    int           force_pure_lc;
    int           decimal_separator;
    /* Settings/Prefix */
    int           prefix_kw[8];
    /* 28 keyword list slots (heap-allocated, may be NULL) */
    char         *kwlists[UDL_KWLIST_COUNT];
    /* Per-style settings */
    UdlStyleEntry styles[UDL_STYLE_TOTAL];
} UdlDefInternal;

static GPtrArray *s_defs = NULL;

/* ------------------------------------------------------------------ */
/* Lookup tables                                                        */
/* ------------------------------------------------------------------ */

static const char * const kKwlistNames[UDL_KWLIST_COUNT] = {
    "Comments",                     /*  0 */
    "Numbers, prefix1",             /*  1 */
    "Numbers, prefix2",             /*  2 */
    "Numbers, extras1",             /*  3 */
    "Numbers, extras2",             /*  4 */
    "Numbers, suffix1",             /*  5 */
    "Numbers, suffix2",             /*  6 */
    "Numbers, range",               /*  7 */
    "Operators1",                   /*  8 */
    "Operators2",                   /*  9 */
    "Folders in code1, open",       /* 10 */
    "Folders in code1, middle",     /* 11 */
    "Folders in code1, close",      /* 12 */
    "Folders in code2, open",       /* 13 */
    "Folders in code2, middle",     /* 14 */
    "Folders in code2, close",      /* 15 */
    "Folders in comment, open",     /* 16 */
    "Folders in comment, middle",   /* 17 */
    "Folders in comment, close",    /* 18 */
    "Keywords1",                    /* 19 */
    "Keywords2",                    /* 20 */
    "Keywords3",                    /* 21 */
    "Keywords4",                    /* 22 */
    "Keywords5",                    /* 23 */
    "Keywords6",                    /* 24 */
    "Keywords7",                    /* 25 */
    "Keywords8",                    /* 26 */
    "Delimiters",                   /* 27 */
};

static int kwlist_name_to_slot(const char *name)
{
    for (int i = 0; i < UDL_KWLIST_COUNT; i++)
        if (strcmp(kKwlistNames[i], name) == 0) return i;
    return -1;
}

typedef struct { const char *name; int id; } StyleNameId;
static const StyleNameId kStyleNameIds[] = {
    { "DEFAULT",            UDL_STYLE_DEFAULT           },
    { "COMMENTS",           UDL_STYLE_COMMENT           },
    { "LINE COMMENTS",      UDL_STYLE_COMMENTLINE       },
    { "NUMBERS",            UDL_STYLE_NUMBER            },
    { "KEYWORDS1",          UDL_STYLE_KEYWORD1          },
    { "KEYWORDS2",          UDL_STYLE_KEYWORD2          },
    { "KEYWORDS3",          UDL_STYLE_KEYWORD3          },
    { "KEYWORDS4",          UDL_STYLE_KEYWORD4          },
    { "KEYWORDS5",          UDL_STYLE_KEYWORD5          },
    { "KEYWORDS6",          UDL_STYLE_KEYWORD6          },
    { "KEYWORDS7",          UDL_STYLE_KEYWORD7          },
    { "KEYWORDS8",          UDL_STYLE_KEYWORD8          },
    { "OPERATORS",          UDL_STYLE_OPERATOR          },
    { "FOLDER IN CODE1",    UDL_STYLE_FOLDER_IN_CODE1   },
    { "FOLDER IN CODE2",    UDL_STYLE_FOLDER_IN_CODE2   },
    { "FOLDER IN COMMENT",  UDL_STYLE_FOLDER_IN_COMMENT },
    { "DELIMITERS1",        UDL_STYLE_DELIMITER1        },
    { "DELIMITERS2",        UDL_STYLE_DELIMITER2        },
    { "DELIMITERS3",        UDL_STYLE_DELIMITER3        },
    { "DELIMITERS4",        UDL_STYLE_DELIMITER4        },
    { "DELIMITERS5",        UDL_STYLE_DELIMITER5        },
    { "DELIMITERS6",        UDL_STYLE_DELIMITER6        },
    { "DELIMITERS7",        UDL_STYLE_DELIMITER7        },
    { "DELIMITERS8",        UDL_STYLE_DELIMITER8        },
    { NULL, -1 }
};

static int style_name_to_id(const char *name)
{
    for (const StyleNameId *sn = kStyleNameIds; sn->name; sn++)
        if (strcmp(sn->name, name) == 0) return sn->id;
    return -1;
}

/* Keyword list slots routed via SCI_SETPROPERTY (index → property name) */
static const struct { int slot; const char *prop; } kPropMap[] = {
    {  0, "userDefine.comments"           },
    {  1, "userDefine.numberPrefix1"      },
    {  2, "userDefine.numberPrefix2"      },
    {  3, "userDefine.numberExtras1"      },
    {  4, "userDefine.numberExtras2"      },
    {  5, "userDefine.numberSuffix1"      },
    {  6, "userDefine.numberSuffix2"      },
    {  7, "userDefine.numberRange"        },
    {  8, "userDefine.operators1"         },
    { 10, "userDefine.foldersInCode1Open"   },
    { 11, "userDefine.foldersInCode1Middle" },
    { 12, "userDefine.foldersInCode1Close"  },
    { 27, "userDefine.delimiters"         },
    { -1, NULL }
};

/* Keyword list slots routed via SCI_SETKEYWORDS, in order (→ counter 0..14) */
static const int kKwSlots[15] = {
    9, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static sptr_t sci_msg(GtkWidget *sci, unsigned int m, uptr_t w, sptr_t l)
{
    return scintilla_send_message(SCINTILLA(sci), m, w, l);
}

/* Convert RRGGBB hex string to Scintilla BGR integer */
static int rrggbb_to_bgr(const char *hex)
{
    if (!hex || !hex[0]) return -1;
    char *end;
    long rgb = strtol(hex, &end, 16);
    if (end == hex) return -1;
    return (int)(((rgb & 0xFF) << 16) | (rgb & 0x00FF00) | ((rgb >> 16) & 0xFF));
}

/* Escape bare & not part of a valid XML entity (same logic as stylestore.c) */
static gchar *fix_bare_ampersands(const gchar *src, gsize len)
{
    GString *out = g_string_sized_new(len + 64);
    for (gsize i = 0; i < len; i++) {
        if (src[i] != '&') { g_string_append_c(out, src[i]); continue; }
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

/* Convert quoted multi-word tokens to \v/\b-separated form expected by LexUser.
 * "hello world" → hello\vworld,  'foo bar' → foo\bbar,  plain → plain */
static char *preprocess_keywords(const char *raw)
{
    if (!raw || !raw[0]) return g_strdup("");
    GString *out = g_string_new(NULL);
    const char *p = raw;
    while (*p) {
        while (*p && g_ascii_isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (out->len > 0) g_string_append_c(out, ' ');
        if (*p == '"' || *p == '\'') {
            char quote  = *p++;
            char sep    = (quote == '"') ? '\v' : '\b';
            gsize tstart = out->len;
            gboolean pending_sep = FALSE;
            while (*p && *p != quote) {
                if (g_ascii_isspace((unsigned char)*p)) {
                    pending_sep = TRUE;
                } else {
                    if (pending_sep && out->len > tstart) {
                        g_string_append_c(out, sep);
                    }
                    pending_sep = FALSE;
                    g_string_append_c(out, *p);
                }
                p++;
            }
            if (*p == quote) p++;
        } else {
            while (*p && !g_ascii_isspace((unsigned char)*p))
                g_string_append_c(out, *p++);
        }
    }
    return g_string_free(out, FALSE);
}

/* Free one UdlDefInternal */
static void udl_def_free(gpointer data)
{
    UdlDefInternal *def = data;
    for (int i = 0; i < UDL_KWLIST_COUNT; i++)
        g_free(def->kwlists[i]);
    g_free(def);
}

/* ------------------------------------------------------------------ */
/* XML parser                                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    PS_NONE,
    PS_NOTEPADPLUS,
    PS_USERLANG,
    PS_SETTINGS,
    PS_KEYWORDLISTS,
    PS_KEYWORDS,
    PS_STYLES,
} ParseState;

typedef struct {
    GPtrArray      *defs;
    UdlDefInternal *cur;
    ParseState      state;
    int             cur_kwlist;
    GString        *kwbuf;
} PCtx;

static void udl_start(GMarkupParseContext *ctx G_GNUC_UNUSED,
                      const gchar *element_name,
                      const gchar **attr_names,
                      const gchar **attr_values,
                      gpointer user_data,
                      GError **error G_GNUC_UNUSED)
{
    PCtx *pc = user_data;

    if (strcmp(element_name, "NotepadPlus") == 0) {
        pc->state = PS_NOTEPADPLUS;

    } else if (strcmp(element_name, "UserLang") == 0 && pc->state == PS_NOTEPADPLUS) {
        UdlDefInternal *def = g_new0(UdlDefInternal, 1);
        for (int i = 0; i < UDL_STYLE_TOTAL; i++) {
            def->styles[i].fg = -1;
            def->styles[i].bg = -1;
        }
        for (int i = 0; attr_names[i]; i++) {
            if (strcmp(attr_names[i], "name") == 0)
                g_strlcpy(def->name, attr_values[i], sizeof(def->name));
            else if (strcmp(attr_names[i], "ext") == 0)
                g_strlcpy(def->ext, attr_values[i], sizeof(def->ext));
        }
        snprintf(def->key, sizeof(def->key), "udl:%s", def->name);
        pc->cur   = def;
        pc->state = PS_USERLANG;

    } else if (strcmp(element_name, "Settings") == 0 && pc->state == PS_USERLANG) {
        pc->state = PS_SETTINGS;

    } else if (pc->state == PS_SETTINGS) {
        if (strcmp(element_name, "Global") == 0) {
            for (int i = 0; attr_names[i]; i++) {
                const char *v = attr_values[i];
                if      (strcmp(attr_names[i], "caseIgnored")         == 0)
                    pc->cur->case_ignored          = strcmp(v, "yes") == 0;
                else if (strcmp(attr_names[i], "allowFoldOfComments") == 0)
                    pc->cur->allow_fold_comments   = strcmp(v, "yes") == 0;
                else if (strcmp(attr_names[i], "foldCompact")         == 0)
                    pc->cur->fold_compact          = strcmp(v, "yes") == 0;
                else if (strcmp(attr_names[i], "forcePureLC")         == 0)
                    pc->cur->force_pure_lc         = atoi(v);
                else if (strcmp(attr_names[i], "decimalSeparator")    == 0)
                    pc->cur->decimal_separator     = atoi(v);
            }
        } else if (strcmp(element_name, "Prefix") == 0) {
            static const char * const kw_attrs[8] = {
                "Keywords1","Keywords2","Keywords3","Keywords4",
                "Keywords5","Keywords6","Keywords7","Keywords8"
            };
            for (int j = 0; j < 8; j++) {
                for (int i = 0; attr_names[i]; i++) {
                    if (strcmp(attr_names[i], kw_attrs[j]) == 0) {
                        pc->cur->prefix_kw[j] = strcmp(attr_values[i], "yes") == 0;
                        break;
                    }
                }
            }
        }

    } else if (strcmp(element_name, "KeywordLists") == 0 && pc->state == PS_USERLANG) {
        pc->state = PS_KEYWORDLISTS;

    } else if (strcmp(element_name, "Keywords") == 0 && pc->state == PS_KEYWORDLISTS) {
        pc->cur_kwlist = -1;
        for (int i = 0; attr_names[i]; i++) {
            if (strcmp(attr_names[i], "name") == 0) {
                pc->cur_kwlist = kwlist_name_to_slot(attr_values[i]);
                break;
            }
        }
        if (pc->cur_kwlist >= 0) {
            if (!pc->kwbuf) pc->kwbuf = g_string_new(NULL);
            else            g_string_truncate(pc->kwbuf, 0);
            pc->state = PS_KEYWORDS;
        }

    } else if (strcmp(element_name, "Styles") == 0 && pc->state == PS_USERLANG) {
        pc->state = PS_STYLES;

    } else if (strcmp(element_name, "WordsStyle") == 0 && pc->state == PS_STYLES && pc->cur) {
        const char *style_name = NULL;
        for (int i = 0; attr_names[i]; i++)
            if (strcmp(attr_names[i], "name") == 0) { style_name = attr_values[i]; break; }
        if (!style_name) return;
        int sid = style_name_to_id(style_name);
        if (sid < 0 || sid >= UDL_STYLE_TOTAL) return;
        UdlStyleEntry *st = &pc->cur->styles[sid];
        for (int i = 0; attr_names[i]; i++) {
            const char *v = attr_values[i];
            if      (strcmp(attr_names[i], "fgColor")    == 0) st->fg         = rrggbb_to_bgr(v);
            else if (strcmp(attr_names[i], "bgColor")    == 0) st->bg         = rrggbb_to_bgr(v);
            else if (strcmp(attr_names[i], "colorStyle") == 0) st->color_style = atoi(v);
            else if (strcmp(attr_names[i], "fontName")   == 0) g_strlcpy(st->font_name, v, sizeof(st->font_name));
            else if (strcmp(attr_names[i], "fontStyle")  == 0) st->font_style = atoi(v);
            else if (strcmp(attr_names[i], "fontSize")   == 0 && v[0]) st->font_size = atoi(v);
            else if (strcmp(attr_names[i], "nesting")    == 0) st->nesting    = atoi(v);
        }
    }
}

static void udl_end(GMarkupParseContext *ctx G_GNUC_UNUSED,
                    const gchar *element_name,
                    gpointer user_data,
                    GError **error G_GNUC_UNUSED)
{
    PCtx *pc = user_data;

    if (strcmp(element_name, "UserLang") == 0 && pc->cur) {
        if (pc->cur->name[0])
            g_ptr_array_add(pc->defs, pc->cur);
        else
            udl_def_free(pc->cur);
        pc->cur   = NULL;
        pc->state = PS_NOTEPADPLUS;

    } else if (strcmp(element_name, "Keywords") == 0 && pc->state == PS_KEYWORDS) {
        if (pc->cur && pc->cur_kwlist >= 0 && pc->kwbuf) {
            g_free(pc->cur->kwlists[pc->cur_kwlist]);
            pc->cur->kwlists[pc->cur_kwlist] = preprocess_keywords(pc->kwbuf->str);
        }
        pc->cur_kwlist = -1;
        pc->state      = PS_KEYWORDLISTS;

    } else if (strcmp(element_name, "Settings")     == 0) { pc->state = PS_USERLANG; }
      else if (strcmp(element_name, "KeywordLists") == 0) { pc->state = PS_USERLANG; }
      else if (strcmp(element_name, "Styles")       == 0) { pc->state = PS_USERLANG; }
      else if (strcmp(element_name, "NotepadPlus")  == 0) { pc->state = PS_NONE;     }
}

static void udl_text(GMarkupParseContext *ctx G_GNUC_UNUSED,
                     const gchar *text, gsize text_len,
                     gpointer user_data,
                     GError **error G_GNUC_UNUSED)
{
    PCtx *pc = user_data;
    if (pc->state == PS_KEYWORDS && pc->kwbuf)
        g_string_append_len(pc->kwbuf, text, (gssize)text_len);
}

static void parse_udl_file(const char *path)
{
    gchar  *contents = NULL;
    gsize   len      = 0;
    GError *err      = NULL;

    if (!g_file_get_contents(path, &contents, &len, &err)) {
        if (err) { g_warning("udl: %s", err->message); g_error_free(err); }
        return;
    }

    gchar *fixed = fix_bare_ampersands(contents, len);
    g_free(contents);

    PCtx pc = { s_defs, NULL, PS_NONE, -1, NULL };
    GMarkupParser parser = { udl_start, udl_end, udl_text, NULL, NULL };
    GMarkupParseContext *ctx =
        g_markup_parse_context_new(&parser, G_MARKUP_DEFAULT_FLAGS, &pc, NULL);

    err = NULL;
    if (!g_markup_parse_context_parse(ctx, fixed, (gssize)strlen(fixed), &err))
        g_warning("udl: parse error in %s: %s", path, err ? err->message : "?");
    if (err) g_error_free(err);
    g_markup_parse_context_free(ctx);
    if (pc.kwbuf) g_string_free(pc.kwbuf, TRUE);
    g_free(fixed);
}

static void load_dir(const char *dir_path)
{
    GError *err = NULL;
    GDir   *dir = g_dir_open(dir_path, 0, &err);
    if (!dir) { if (err) g_error_free(err); return; }
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_suffix(name, ".xml")) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dir_path, name);
            parse_udl_file(path);
        }
    }
    g_dir_close(dir);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void udl_load_all(void)
{
    if (s_defs) return;
    s_defs = g_ptr_array_new_with_free_func(udl_def_free);

    char bundled_dir[512];
    snprintf(bundled_dir, sizeof(bundled_dir), "%s/userDefineLangs", RESOURCES_DIR);
    load_dir(bundled_dir);

    const char *home = g_get_home_dir();
    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "%s/.config/notetux/userDefineLangs", home);
    load_dir(user_dir);
}

int udl_count(void)
{
    return s_defs ? (int)s_defs->len : 0;
}

const char *udl_name(int i)
{
    if (!s_defs || i < 0 || i >= (int)s_defs->len) return NULL;
    return ((UdlDefInternal *)g_ptr_array_index(s_defs, i))->name;
}

const char *udl_key(int i)
{
    if (!s_defs || i < 0 || i >= (int)s_defs->len) return NULL;
    return ((UdlDefInternal *)g_ptr_array_index(s_defs, i))->key;
}

int udl_find_by_name(const char *name)
{
    if (!s_defs || !name || !name[0]) return -1;
    for (guint i = 0; i < s_defs->len; i++)
        if (strcmp(((UdlDefInternal *)g_ptr_array_index(s_defs, i))->name, name) == 0)
            return (int)i;
    return -1;
}

int udl_find_by_ext(const char *ext)
{
    if (!s_defs || !ext || !ext[0]) return -1;
    char low[64];
    int k = 0;
    while (ext[k] && k < 63) { low[k] = (char)tolower((unsigned char)ext[k]); k++; }
    low[k] = '\0';

    for (guint di = 0; di < s_defs->len; di++) {
        UdlDefInternal *def = g_ptr_array_index(s_defs, di);
        gchar **parts = g_strsplit(def->ext, " ", -1);
        for (int j = 0; parts[j]; j++) {
            if (g_ascii_strcasecmp(parts[j], low) == 0) {
                g_strfreev(parts);
                return (int)di;
            }
        }
        g_strfreev(parts);
    }
    return -1;
}

void udl_apply(GtkWidget *sci, int index)
{
    if (!s_defs || index < 0 || index >= (int)s_defs->len) return;
    UdlDefInternal *def = g_ptr_array_index(s_defs, index);

    /* Full style pipeline */
    stylestore_apply_default(sci);
    sci_msg(sci, SCI_STYLECLEARALL, 0, 0);
    stylestore_apply_global(sci);

    /* Install the "user" (UDL) lexer */
    void *lexer = lexilla_create_lexer("user");
    if (!lexer) return;
    sci_msg(sci, SCI_SETILEXER, 0, (sptr_t)lexer);

    /* Behavior properties */
    sci_msg(sci, SCI_SETPROPERTY,
            (uptr_t)"userDefine.isCaseIgnored",
            (sptr_t)(def->case_ignored ? "1" : "0"));
    sci_msg(sci, SCI_SETPROPERTY,
            (uptr_t)"userDefine.allowFoldOfComments",
            (sptr_t)(def->allow_fold_comments ? "1" : "0"));
    sci_msg(sci, SCI_SETPROPERTY,
            (uptr_t)"userDefine.foldCompact",
            (sptr_t)(def->fold_compact ? "1" : "0"));

    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", def->force_pure_lc);
    sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"userDefine.forcePureLC", (sptr_t)tmp);

    snprintf(tmp, sizeof(tmp), "%d", def->decimal_separator);
    sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"userDefine.decimalSeparator", (sptr_t)tmp);

    /* Prefix-keyword flags */
    static const char * const kPrefixProps[8] = {
        "userDefine.prefixKeywords1", "userDefine.prefixKeywords2",
        "userDefine.prefixKeywords3", "userDefine.prefixKeywords4",
        "userDefine.prefixKeywords5", "userDefine.prefixKeywords6",
        "userDefine.prefixKeywords7", "userDefine.prefixKeywords8",
    };
    for (int i = 0; i < 8; i++)
        sci_msg(sci, SCI_SETPROPERTY,
                (uptr_t)kPrefixProps[i],
                (sptr_t)(def->prefix_kw[i] ? "1" : "0"));

    /* Stable UDL/buffer IDs for LexUser's internal caches */
    snprintf(tmp, sizeof(tmp), "%d", index);
    sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"userDefine.udlName", (sptr_t)tmp);
    sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"userDefine.currentBufferID", (sptr_t)"0");

    /* Nesting per style */
    for (int s = 0; s < UDL_STYLE_TOTAL; s++) {
        if (def->styles[s].nesting) {
            char propname[32], propval[32];
            snprintf(propname, sizeof(propname), "userDefine.nesting.%d", s);
            snprintf(propval,  sizeof(propval),  "%d", def->styles[s].nesting);
            sci_msg(sci, SCI_SETPROPERTY, (uptr_t)propname, (sptr_t)propval);
        }
    }

    /* Keyword lists → SCI_SETPROPERTY */
    for (int i = 0; kPropMap[i].prop; i++) {
        const char *val = def->kwlists[kPropMap[i].slot];
        sci_msg(sci, SCI_SETPROPERTY,
                (uptr_t)kPropMap[i].prop,
                (sptr_t)(val ? val : ""));
    }

    /* Keyword lists → SCI_SETKEYWORDS (counter 0..14) */
    for (int i = 0; i < 15; i++) {
        const char *val = def->kwlists[kKwSlots[i]];
        sci_msg(sci, SCI_SETKEYWORDS, (uptr_t)i, (sptr_t)(val ? val : ""));
    }

    /* Per-style colors and fonts */
    for (int s = 0; s < UDL_STYLE_TOTAL; s++) {
        UdlStyleEntry *st = &def->styles[s];
        if (st->color_style == 1) {
            if (st->fg >= 0) sci_msg(sci, SCI_STYLESETFORE,      s, st->fg);
            if (st->bg >= 0) sci_msg(sci, SCI_STYLESETBACK,      s, st->bg);
        }
        sci_msg(sci, SCI_STYLESETBOLD,      s, (st->font_style & 1) != 0);
        sci_msg(sci, SCI_STYLESETITALIC,    s, (st->font_style & 2) != 0);
        sci_msg(sci, SCI_STYLESETUNDERLINE, s, (st->font_style & 4) != 0);
        if (st->font_name[0])
            sci_msg(sci, SCI_STYLESETFONT, s, (sptr_t)st->font_name);
        if (st->font_size > 0)
            sci_msg(sci, SCI_STYLESETSIZE, s, st->font_size);
    }

    /* Trigger re-colourization */
    sptr_t docLen = sci_msg(sci, SCI_GETLENGTH, 0, 0);
    if (docLen > 0)
        sci_msg(sci, SCI_COLOURISE, 0, docLen);
}
