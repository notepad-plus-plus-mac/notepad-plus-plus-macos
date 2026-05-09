#include "funclist.h"
#include "editor.h"
#include "sci_c.h"
#include <gtk/gtk.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tree model columns                                                 */
/* ------------------------------------------------------------------ */

enum {
    COL_LINE,   /* int:  1-based line number; -1 for group headers */
    COL_NAME,   /* str:  display text                              */
    N_COLS
};

/* ------------------------------------------------------------------ */
/* Per-language patterns                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *lang;
    const char *func_re; /* PCRE; capture group 1 = function name */
    const char *grp_re;  /* PCRE; capture group 1 = class/struct name; NULL = flat */
} LangPat;

static const LangPat kLang[] = {
    /* C ---------------------------------------------------------------- */
    { "c",
      "^(?![ \\t]*(?:#|//))(?:[\\w_]+[ \\t]+){1,5}\\**([A-Za-z_]\\w*)[ \\t]*\\(",
      "^(?:struct|union|enum)[ \\t]+([A-Za-z_]\\w*)" },
    /* C++ --------------------------------------------------------------- */
    { "cpp",
      "^(?![ \\t]*(?:#|//))(?:[\\w_:~]\\S*[ \\t]+){1,6}\\**([A-Za-z_~]\\w*(?:::[A-Za-z_~]\\w*)*)[ \\t]*\\(",
      "^(?:class|struct|namespace)[ \\t]+([A-Za-z_]\\w*)" },
    /* Objective-C ------------------------------------------------------- */
    { "objc",
      "^[-+][ \\t]*\\([^)]*\\)[ \\t]*([A-Za-z_]\\w*)(?:[ \\t]*:|[ \\t]*[{;])",
      "^@(?:interface|implementation|protocol)[ \\t]+([A-Za-z_]\\w*)" },
    /* Python ------------------------------------------------------------ */
    { "python",
      "^([ \\t]*)def[ \\t]+([A-Za-z_]\\w*)[ \\t]*\\(",  /* grp1=indent, grp2=name */
      "^([ \\t]*)class[ \\t]+([A-Za-z_]\\w*)" },         /* grp1=indent, grp2=name */
    /* JavaScript -------------------------------------------------------- */
    { "javascript",
      "^[ \\t]*(?:export[ \\t]+)?(?:async[ \\t]+)?function[ \\t]+([A-Za-z_$][\\w$]*)[ \\t]*\\(",
      "^[ \\t]*(?:export[ \\t]+)?(?:default[ \\t]+)?class[ \\t]+([A-Za-z_$][\\w$]*)" },
    /* TypeScript -------------------------------------------------------- */
    { "typescript",
      "^[ \\t]*(?:export[ \\t]+)?(?:async[ \\t]+)?function[ \\t]+([A-Za-z_$][\\w$]*)[ \\t]*[(<]",
      "^[ \\t]*(?:export[ \\t]+)?(?:abstract[ \\t]+)?class[ \\t]+([A-Za-z_$][\\w$]*)" },
    /* Java -------------------------------------------------------------- */
    { "java",
      "^[ \\t]+(?:[\\w<>\\[\\]]+[ \\t]+)+([A-Za-z_]\\w*)[ \\t]*\\(",
      "^[ \\t]*(?:[\\w]+[ \\t]+)*(?:class|interface|enum)[ \\t]+([A-Za-z_]\\w*)" },
    /* C# ---------------------------------------------------------------- */
    { "cs",
      "^[ \\t]+(?:[\\w<>\\[\\]\\.]+[ \\t]+)+([A-Za-z_]\\w*)[ \\t]*\\(",
      "^[ \\t]*(?:[\\w]+[ \\t]+)*(?:class|interface|struct|enum)[ \\t]+([A-Za-z_]\\w*)" },
    /* Go ---------------------------------------------------------------- */
    { "go",
      "^func[ \\t]+(?:\\([^)]*\\)[ \\t]+)?([A-Za-z_]\\w*)[ \\t]*[(<]",
      "^type[ \\t]+([A-Za-z_]\\w*)[ \\t]+struct" },
    /* Rust -------------------------------------------------------------- */
    { "rust",
      "^[ \\t]*(?:pub(?:\\([\\w]+\\))?[ \\t]+)?(?:async[ \\t]+)?fn[ \\t]+([A-Za-z_]\\w*)[ \\t]*[(<]",
      "^[ \\t]*(?:pub(?:\\([\\w]+\\))?[ \\t]+)?(?:struct|enum|trait|impl)[ \\t]+([A-Za-z_]\\w*)" },
    /* PHP --------------------------------------------------------------- */
    { "php",
      "^[ \\t]*(?:(?:public|private|protected|static|abstract|final)[ \\t]+)*function[ \\t]+([A-Za-z_]\\w*)[ \\t]*\\(",
      "^[ \\t]*(?:[\\w]+[ \\t]+)*(?:class|interface|trait)[ \\t]+([A-Za-z_]\\w*)" },
    /* Ruby -------------------------------------------------------------- */
    { "ruby",
      "^[ \\t]*def[ \\t]+([A-Za-z_][\\w!?]*)(?:[ \\t]|$|\\()",
      "^[ \\t]*(?:class|module)[ \\t]+([A-Za-z_]\\w*)" },
    /* Bash -------------------------------------------------------------- */
    { "bash",
      "^([A-Za-z_][\\w-]*)[ \\t]*\\([ \\t]*\\)",
      NULL },
    /* Lua --------------------------------------------------------------- */
    { "lua",
      "^(?:local[ \\t]+)?function[ \\t]+([A-Za-z_][\\w\\.]*)[ \\t]*\\(",
      NULL },
    /* Swift ------------------------------------------------------------- */
    { "swift",
      "^[ \\t]*(?:[\\w]+[ \\t]+)*func[ \\t]+([A-Za-z_]\\w*)[ \\t]*[(<]",
      "^[ \\t]*(?:[\\w]+[ \\t]+)*(?:class|struct|enum|protocol|extension)[ \\t]+([A-Za-z_]\\w*)" },
    /* Kotlin ------------------------------------------------------------ */
    { "kotlin",
      "^[ \\t]*(?:[\\w]+[ \\t]+)*fun[ \\t]+([A-Za-z_]\\w*)[ \\t]*[(<]",
      "^[ \\t]*(?:data[ \\t]+)?(?:class|interface|object)[ \\t]+([A-Za-z_]\\w*)" },
    /* Perl -------------------------------------------------------------- */
    { "perl",
      "^sub[ \\t]+([A-Za-z_]\\w*)[ \\t]*(?:\\{|$)",
      NULL },
    /* SQL --------------------------------------------------------------- */
    { "sql",
      "^[ \\t]*(?:CREATE|create)[ \\t]+(?:OR[ \\t]+REPLACE[ \\t]+)?(?:FUNCTION|PROCEDURE|TRIGGER|VIEW|function|procedure|trigger|view)[ \\t]+([A-Za-z_][\\w#$]*)",
      NULL },
    /* PowerShell -------------------------------------------------------- */
    { "powershell",
      "^[ \\t]*function[ \\t]+([A-Za-z_][\\w-]*)[ \\t]*(?:\\{|\\(|$)",
      NULL },
    { NULL, NULL, NULL }
};

/* ------------------------------------------------------------------ */
/* Compiled pattern cache (compiled once on first use)               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *lang;
    GRegex     *func_re;
    GRegex     *grp_re;   /* NULL if no class grouping */
    gboolean    is_python; /* Python uses indentation instead of braces */
} CompiledPat;

static CompiledPat  s_compiled[32];
static int          s_ncompiled = 0;
static gboolean     s_ready = FALSE;

static void ensure_compiled(void)
{
    if (s_ready) return;
    for (int i = 0; kLang[i].lang && i < 32; i++) {
        s_compiled[i].lang      = kLang[i].lang;
        s_compiled[i].is_python = (strcmp(kLang[i].lang, "python") == 0);
        s_compiled[i].func_re   = g_regex_new(kLang[i].func_re,
                                               G_REGEX_OPTIMIZE, 0, NULL);
        s_compiled[i].grp_re    = kLang[i].grp_re
            ? g_regex_new(kLang[i].grp_re, G_REGEX_OPTIMIZE, 0, NULL)
            : NULL;
        s_ncompiled++;
    }
    s_ready = TRUE;
}

static CompiledPat *find_pattern(const char *lang)
{
    if (!lang) return NULL;
    ensure_compiled();
    for (int i = 0; i < s_ncompiled; i++)
        if (strcmp(s_compiled[i].lang, lang) == 0) return &s_compiled[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Module state                                                       */
/* ------------------------------------------------------------------ */

static GtkWidget    *s_panel   = NULL;
static GtkWidget    *s_tree    = NULL;
static GtkTreeStore *s_store   = NULL;
static guint         s_timer   = 0;
static GtkWidget    *s_pending_sci = NULL;

/* ------------------------------------------------------------------ */
/* Brace-depth tracker (ignores strings and line comments)           */
/* ------------------------------------------------------------------ */

static void count_braces(const char *line, int *depth)
{
    gboolean in_str  = FALSE;
    char     str_ch  = 0;
    for (const char *p = line; *p; p++) {
        if (in_str) {
            if (*p == '\\') { p++; continue; }
            if (*p == str_ch) in_str = FALSE;
        } else if (*p == '"' || *p == '\'') {
            in_str = TRUE; str_ch = *p;
        } else if (*p == '/' && *(p + 1) == '/') {
            break;
        } else if (*p == '{') {
            (*depth)++;
        } else if (*p == '}') {
            if (*depth > 0) (*depth)--;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Leading-whitespace length (for Python indentation detection)       */
/* ------------------------------------------------------------------ */

static int indent_of(const char *line)
{
    int n = 0;
    while (line[n] == ' ' || line[n] == '\t') n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Ensure a "(Global)" root node exists and return its iter          */
/* ------------------------------------------------------------------ */

static GtkTreeIter s_global_iter;
static gboolean    s_global_valid = FALSE;

static GtkTreeIter *get_global(void)
{
    if (!s_global_valid) {
        gtk_tree_store_append(s_store, &s_global_iter, NULL);
        gtk_tree_store_set(s_store, &s_global_iter,
                           COL_LINE, -1,
                           COL_NAME, "(Global)",
                           -1);
        s_global_valid = TRUE;
    }
    return &s_global_iter;
}

/* ------------------------------------------------------------------ */
/* Parse and populate the tree                                        */
/* ------------------------------------------------------------------ */

static void do_parse(GtkWidget *sci)
{
    if (!s_store || !sci) return;

    /* Determine language */
    const char *lang = (const char *)g_object_get_data(G_OBJECT(sci), "npp-lang");
    CompiledPat *pat = find_pattern(lang);

    /* Clear tree */
    gtk_tree_store_clear(s_store);
    s_global_valid = FALSE;

    if (!pat) return; /* unsupported language — leave tree empty */

    /* Fetch full document text */
    int total = (int)scintilla_send_message(SCINTILLA(sci), SCI_GETLENGTH, 0, 0);
    if (total <= 0) return;
    char *text = g_malloc(total + 1);
    scintilla_send_message(SCINTILLA(sci), SCI_GETTEXT, (uptr_t)(total + 1), (sptr_t)text);

    /* State */
    int          brace_depth    = 0;
    int          class_depth    = -1; /* brace depth at class entry */
    GtkTreeIter  cur_grp_iter;
    gboolean     cur_grp_valid  = FALSE;
    int          py_class_indent = -1; /* Python: indent level of last class */

    /* Iterate line by line */
    int   line_no = 1; /* 1-based */
    char *pos     = text;
    while (*pos) {
        char *nl  = strpbrk(pos, "\r\n");
        size_t ln = nl ? (size_t)(nl - pos) : strlen(pos);

        /* Copy this line (NUL-terminated, stripped of EOL) */
        char *line = g_malloc(ln + 1);
        memcpy(line, pos, ln);
        line[ln] = '\0';

        /* ---- Non-Python: track brace depth ---- */
        if (!pat->is_python) {
            count_braces(line, &brace_depth);
            /* When brace_depth drops to class entry depth, class has ended */
            if (cur_grp_valid && brace_depth <= class_depth) {
                cur_grp_valid = FALSE;
                class_depth   = -1;
            }
        }

        /* ---- Try group (class) pattern ---- */
        if (pat->grp_re) {
            GMatchInfo *mi = NULL;
            if (g_regex_match(pat->grp_re, line, 0, &mi)) {
                char *name = NULL;
                if (pat->is_python) {
                    /* group 1=indent_str, group 2=name */
                    char *indent_str = g_match_info_fetch(mi, 1);
                    name = g_match_info_fetch(mi, 2);
                    py_class_indent = indent_str ? (int)strlen(indent_str) : 0;
                    g_free(indent_str);
                } else {
                    name = g_match_info_fetch(mi, 1);
                }
                if (name && *name) {
                    gtk_tree_store_append(s_store, &cur_grp_iter, NULL);
                    gtk_tree_store_set(s_store, &cur_grp_iter,
                                       COL_LINE, line_no,
                                       COL_NAME, name,
                                       -1);
                    cur_grp_valid = TRUE;
                    class_depth   = brace_depth - 1; /* body opens at depth+1 */
                }
                g_free(name);
            }
            g_match_info_free(mi);
        }

        /* ---- Try function pattern ---- */
        {
            GMatchInfo *mi = NULL;
            if (g_regex_match(pat->func_re, line, 0, &mi)) {
                char *name = NULL;
                gboolean is_method = cur_grp_valid;

                if (pat->is_python) {
                    /* group 1=indent_str, group 2=name */
                    char *indent_str = g_match_info_fetch(mi, 1);
                    name = g_match_info_fetch(mi, 2);
                    int func_indent = indent_str ? (int)strlen(indent_str) : 0;
                    /* Method only if indented more than class declaration */
                    is_method = (cur_grp_valid && py_class_indent >= 0
                                 && func_indent > py_class_indent);
                    /* If not a method, it resets the class context */
                    if (!is_method) {
                        cur_grp_valid  = FALSE;
                        py_class_indent = -1;
                    }
                    g_free(indent_str);
                } else {
                    name = g_match_info_fetch(mi, 1);
                }

                if (name && *name) {
                    GtkTreeIter entry;
                    GtkTreeIter *parent = is_method ? &cur_grp_iter : get_global();
                    gtk_tree_store_append(s_store, &entry, parent);
                    gtk_tree_store_set(s_store, &entry,
                                       COL_LINE, line_no,
                                       COL_NAME, name,
                                       -1);
                }
                g_free(name);
            }
            g_match_info_free(mi);
        }

        g_free(line);

        /* Advance to next line */
        if (nl) {
            if (*nl == '\r' && *(nl + 1) == '\n') nl++;
            pos = nl + 1;
        } else {
            break;
        }
        line_no++;
    }

    g_free(text);

    /* Remove "(Global)" if it ended up with no children */
    if (s_global_valid) {
        if (!gtk_tree_model_iter_has_child(GTK_TREE_MODEL(s_store), &s_global_iter)) {
            gtk_tree_store_remove(s_store, &s_global_iter);
            s_global_valid = FALSE;
        }
    }

    gtk_tree_view_expand_all(GTK_TREE_VIEW(s_tree));
}

/* ------------------------------------------------------------------ */
/* Debounce timer                                                     */
/* ------------------------------------------------------------------ */

static gboolean on_timer_fire(gpointer d)
{
    (void)d;
    s_timer = 0;
    do_parse(s_pending_sci);
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/* Cell data func — bold group header rows (COL_LINE == -1)           */
/* ------------------------------------------------------------------ */

static void render_name(GtkTreeViewColumn *col, GtkCellRenderer *rend,
                        GtkTreeModel *model, GtkTreeIter *iter, gpointer d)
{
    (void)col; (void)d;
    int ln;
    gtk_tree_model_get(model, iter, COL_LINE, &ln, -1);
    g_object_set(rend, "weight",
                 ln < 0 ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                 NULL);
}

/* ------------------------------------------------------------------ */
/* Signal handlers                                                    */
/* ------------------------------------------------------------------ */

static void on_row_activated(GtkTreeView *tv, GtkTreePath *path,
                              GtkTreeViewColumn *col, gpointer d)
{
    (void)tv; (void)col; (void)d;
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(s_store), &iter, path)) return;

    int line;
    gtk_tree_model_get(GTK_TREE_MODEL(s_store), &iter, COL_LINE, &line, -1);
    if (line < 1) return; /* group header */

    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    /* Jump to line (0-based in Scintilla) */
    scintilla_send_message(SCINTILLA(doc->sci), SCI_GOTOLINE, (uptr_t)(line - 1), 0);
    scintilla_send_message(SCINTILLA(doc->sci), SCI_SCROLLCARET, 0, 0);
    gtk_widget_grab_focus(doc->sci);
}

static void on_close_clicked(GtkButton *btn, gpointer d)
{
    (void)btn; (void)d;
    funclist_set_visible(FALSE);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

GtkWidget *funclist_init(void)
{
    s_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(s_panel, 200, -1);

    /* Header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    GtkWidget *title = gtk_label_new("Function List");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 6);

    GtkWidget *close_btn = gtk_button_new_with_label("×");
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header), close_btn, FALSE, FALSE, 2);

    gtk_box_pack_start(GTK_BOX(s_panel), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(s_panel),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);

    /* Tree model */
    s_store = gtk_tree_store_new(N_COLS, G_TYPE_INT, G_TYPE_STRING);

    /* Tree view */
    s_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(s_tree), FALSE);

    /* Single column: name; group rows rendered bold */
    GtkTreeViewColumn *col  = gtk_tree_view_column_new();
    GtkCellRenderer   *rend = gtk_cell_renderer_text_new();
    g_object_set(rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_column_pack_start(col, rend, TRUE);
    gtk_tree_view_column_add_attribute(col, rend, "text", COL_NAME);
    /* Bold for group headers (line == -1) */
    gtk_tree_view_column_set_cell_data_func(col, rend, render_name, NULL, NULL);

    gtk_tree_view_append_column(GTK_TREE_VIEW(s_tree), col);
    g_signal_connect(s_tree, "row-activated", G_CALLBACK(on_row_activated), NULL);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), s_tree);
    gtk_box_pack_start(GTK_BOX(s_panel), scroll, TRUE, TRUE, 0);

    gtk_widget_hide(s_panel);
    return s_panel;
}

void funclist_update(GtkWidget *sci)
{
    s_pending_sci = sci;
    if (s_timer) { g_source_remove(s_timer); s_timer = 0; }
    do_parse(sci);
}

void funclist_schedule_update(GtkWidget *sci)
{
    s_pending_sci = sci;
    if (s_timer) g_source_remove(s_timer);
    s_timer = g_timeout_add(600, on_timer_fire, NULL);
}

void funclist_set_visible(gboolean v)
{
    if (!s_panel) return;
    if (v) gtk_widget_show(s_panel);
    else   gtk_widget_hide(s_panel);
}

gboolean funclist_is_visible(void)
{
    return s_panel && gtk_widget_get_visible(s_panel);
}
