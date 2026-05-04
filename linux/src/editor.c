#include "editor.h"
#include "encoding.h"
#include "prefs.h"
#include "statusbar.h"
#include "lexer.h"
#include "findreplace.h"
#include "toolbar.h"
#include "stylestore.h"
#include "i18n.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static GtkWidget *s_notebook;
static GtkWidget *s_window;
static int        s_new_count;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static sptr_t sci_msg(GtkWidget *sci, unsigned int m, uptr_t w, sptr_t l)
{
    return scintilla_send_message(SCINTILLA(sci), m, w, l);
}

static NppDoc *doc_of_sci(GtkWidget *sci)
{
    return g_object_get_data(G_OBJECT(sci), "npp-doc");
}

static GtkWidget *sci_of_page(int page)
{
    return gtk_notebook_get_nth_page(GTK_NOTEBOOK(s_notebook), page);
}

static void setup_sci(GtkWidget *sci)
{
    sci_msg(sci, SCI_SETCODEPAGE,     SC_CP_UTF8, 0);
    /* Margin 0: line numbers */
    sci_msg(sci, SCI_SETMARGINTYPE,      0, SC_MARGIN_NUMBER);
    sci_msg(sci, SCI_SETMARGINWIDTHN,    0, 44);
    /* Margin 1: bookmarks */
    sci_msg(sci, SCI_SETMARGINTYPE,      1, SC_MARGIN_SYMBOL);
    sci_msg(sci, SCI_SETMARGINSENSITIVE, 1, 1);
    sci_msg(sci, SCI_SETMARGINWIDTHN,    1, 0);       /* hidden until toggled via View menu */
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_BOOKMARK, SC_MARK_BOOKMARK);
    sci_msg(sci, SCI_MARKERSETFORE, SC_MARKNUM_BOOKMARK, 0x0000FF); /* blue */
    sci_msg(sci, SCI_MARKERSETBACK, SC_MARKNUM_BOOKMARK, 0x0080FF);
    /* Margin 2: fold */
    sci_msg(sci, SCI_SETMARGINTYPE,      2, SC_MARGIN_SYMBOL);
    sci_msg(sci, SCI_SETMARGINSENSITIVE, 2, 1);
    sci_msg(sci, SCI_SETMARGINWIDTHN,    2, 14);
    /* Mark-style indicators 0–4: ROUNDBOX with semi-transparent fill */
    static const int mark_colors[5] = {
        0x00FFFF, /* yellow  (BGR) */
        0x00FF00, /* cyan    (BGR) */
        0xFF8000, /* blue    (BGR) */
        0x0080FF, /* orange  (BGR) */
        0x8000FF, /* magenta (BGR) */
    };
    for (int k = 0; k < 5; k++) {
        sci_msg(sci, SCI_INDICSETSTYLE, (uptr_t)k, INDIC_ROUNDBOX);
        sci_msg(sci, SCI_INDICSETFORE,  (uptr_t)k, mark_colors[k]);
        sci_msg(sci, SCI_INDICSETALPHA, (uptr_t)k, 100);
    }
    sci_msg(sci, SCI_SETVIRTUALSPACEOPTIONS,
            SCVS_RECTANGULARSELECTION | SCVS_USERACCESSIBLE, 0);
    sci_msg(sci, SCI_SETTABWIDTH,        (uptr_t)g_prefs.tab_width,  0);
    sci_msg(sci, SCI_SETUSETABS,         (uptr_t)g_prefs.use_tabs,   0);
    sci_msg(sci, SCI_SETCARETLINEVISIBLE,(uptr_t)g_prefs.highlight_current_line, 0);
    sci_msg(sci, SCI_SETCARETWIDTH,      (uptr_t)g_prefs.caret_width, 0);
    sci_msg(sci, SCI_SETCARETPERIOD,     (uptr_t)g_prefs.caret_blink_rate, 0);
    sci_msg(sci, SCI_SETENDATLASTLINE,   g_prefs.scroll_beyond_last_line ? 0 : 1, 0);
    /* Apply theme: STYLE_DEFAULT must be set before STYLECLEARALL */
    stylestore_apply_default(sci);
    sci_msg(sci, SCI_STYLECLEARALL, 0, 0);
    stylestore_apply_global(sci);
}

/* ------------------------------------------------------------------ */
/* Tab label (filename + close button)                                */
/* ------------------------------------------------------------------ */

static void on_close_btn_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn;
    GtkWidget *sci = (GtkWidget *)data;
    int page = gtk_notebook_page_num(GTK_NOTEBOOK(s_notebook), sci);
    editor_close_page(page);
}

static GtkWidget *make_tab_label(NppDoc *doc, GtkWidget *sci)
{
    const char *base = doc->filepath
        ? g_path_get_basename(doc->filepath)
        : NULL;
    char buf[64];
    if (base)
        snprintf(buf, sizeof(buf), "%s", base);
    else
        snprintf(buf, sizeof(buf), "new %d", doc->new_index);

    GtkWidget *box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *label = gtk_label_new(buf);
    GtkWidget *img   = gtk_image_new_from_icon_name("window-close-symbolic",
                                                     GTK_ICON_SIZE_MENU);
    GtkWidget *btn   = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(btn), img);
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(btn, FALSE);

    g_signal_connect(btn, "clicked", G_CALLBACK(on_close_btn_clicked), sci);

    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn,   FALSE, FALSE, 0);
    gtk_widget_show_all(box);

    /* store label widget on sci for later updates */
    g_object_set_data(G_OBJECT(sci), "tab-label", label);
    return box;
}

static void refresh_tab_label(int page)
{
    GtkWidget *sci = sci_of_page(page);
    if (!sci) return;
    NppDoc *doc = doc_of_sci(sci);
    GtkWidget *label = g_object_get_data(G_OBJECT(sci), "tab-label");
    if (!label || !doc) return;

    const char *base = doc->filepath
        ? g_path_get_basename(doc->filepath)
        : NULL;
    char buf[80];
    if (base)
        snprintf(buf, sizeof(buf), "%s%s", doc->modified ? "*" : "", base);
    else
        snprintf(buf, sizeof(buf), "%snew %d", doc->modified ? "*" : "", doc->new_index);

    gtk_label_set_text(GTK_LABEL(label), buf);
}

static void update_window_title(void)
{
    if (!s_window) return;
    NppDoc *doc = editor_current_doc();
    if (!doc) return;

    const char *mod = doc->modified ? "*" : "";
    char buf[512];
    if (doc->filepath) {
        const char *name = g_prefs.show_full_path_in_title
                           ? doc->filepath
                           : g_path_get_basename(doc->filepath);
        snprintf(buf, sizeof(buf), "%s%s — Notepad++ Linux", mod, name);
    } else {
        snprintf(buf, sizeof(buf), "%snew %d — Notepad++ Linux", mod, doc->new_index);
    }

    gtk_window_set_title(GTK_WINDOW(s_window), buf);
}

/* ------------------------------------------------------------------ */
/* Scintilla notification handler                                      */
/* ------------------------------------------------------------------ */

static void on_sci_notify(GtkWidget *sci, gint unused,
                          SCNotification *n, gpointer data)
{
    (void)unused; (void)data;
    NppDoc *doc = doc_of_sci(sci);
    if (!doc) return;

    unsigned int code = n->nmhdr.code;

    if (code == SCN_SAVEPOINTREACHED) {
        doc->modified = FALSE;
        int page = gtk_notebook_page_num(GTK_NOTEBOOK(s_notebook), sci);
        refresh_tab_label(page);
        update_window_title();
    } else if (code == SCN_SAVEPOINTLEFT) {
        doc->modified = TRUE;
        int page = gtk_notebook_page_num(GTK_NOTEBOOK(s_notebook), sci);
        refresh_tab_label(page);
        update_window_title();
    } else if (code == SCN_UPDATEUI) {
        /* only update statusbar for the currently visible tab */
        int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(s_notebook));
        if (gtk_notebook_get_nth_page(GTK_NOTEBOOK(s_notebook), cur) == sci)
            statusbar_update_from_sci(sci);

        /* Brace highlighting */
        Sci_Position pos = (Sci_Position)sci_msg(sci, SCI_GETCURRENTPOS, 0, 0);
        static const char braces[] = "()[]{}<>";
        Sci_Position brace_pos = -1;
        char ch = (char)sci_msg(sci, SCI_GETCHARAT, (uptr_t)pos, 0);
        if (strchr(braces, ch))
            brace_pos = pos;
        else {
            ch = (char)sci_msg(sci, SCI_GETCHARAT, (uptr_t)(pos - 1), 0);
            if (pos > 0 && strchr(braces, ch))
                brace_pos = pos - 1;
        }
        if (brace_pos >= 0) {
            Sci_Position match = (Sci_Position)sci_msg(sci, SCI_BRACEMATCH, (uptr_t)brace_pos, 0);
            if (match >= 0)
                sci_msg(sci, SCI_BRACEHIGHLIGHT, (uptr_t)brace_pos, (sptr_t)match);
            else
                sci_msg(sci, SCI_BRACEBADLIGHT, (uptr_t)brace_pos, 0);
        } else {
            sci_msg(sci, SCI_BRACEHIGHLIGHT, (uptr_t)-1, (sptr_t)-1);
        }
    } else if (code == SCN_MARGINCLICK) {
        if (n->margin == 1)
            main_toggle_bookmark_at_line(sci, (int)n->line);
    } else if (code == SCN_CHARADDED && g_prefs.auto_indent != AUTO_INDENT_NONE) {
        if (n->ch == '\n' || n->ch == '\r') {
            Sci_Position cur_line = (Sci_Position)sci_msg(sci, SCI_LINEFROMPOSITION,
                (uptr_t)sci_msg(sci, SCI_GETCURRENTPOS, 0, 0), 0);
            Sci_Position prev_line = cur_line - 1;
            if (prev_line < 0) return;

            int indent = (int)sci_msg(sci, SCI_GETLINEINDENTATION, (uptr_t)prev_line, 0);
            int tab_w  = (int)sci_msg(sci, SCI_GETTABWIDTH, 0, 0);
            if (tab_w < 1) tab_w = 4;

            if (g_prefs.auto_indent >= AUTO_INDENT_BASIC + 1) {
                /* Advanced: look at the last non-whitespace char of prev line */
                Sci_Position line_start = (Sci_Position)sci_msg(sci,
                    SCI_POSITIONFROMLINE, (uptr_t)prev_line, 0);
                Sci_Position line_end   = (Sci_Position)sci_msg(sci,
                    SCI_GETLINEENDPOSITION, (uptr_t)prev_line, 0);
                char last_ch = 0;
                for (Sci_Position p = line_end - 1; p >= line_start; p--) {
                    char c = (char)sci_msg(sci, SCI_GETCHARAT, (uptr_t)p, 0);
                    if (c != ' ' && c != '\t') { last_ch = c; break; }
                }
                if (last_ch == '{' || last_ch == ':')
                    indent += tab_w;

                /* If the new line (being typed) starts with '}', dedent it */
                Sci_Position cur_start = (Sci_Position)sci_msg(sci,
                    SCI_POSITIONFROMLINE, (uptr_t)cur_line, 0);
                char first_ch = (char)sci_msg(sci, SCI_GETCHARAT, (uptr_t)cur_start, 0);
                if (first_ch == '}' && indent >= tab_w)
                    indent -= tab_w;
            }

            sci_msg(sci, SCI_SETLINEINDENTATION, (uptr_t)cur_line, (sptr_t)indent);
            /* Move caret to end of new indentation */
            Sci_Position new_pos = (Sci_Position)sci_msg(sci,
                SCI_GETLINEINDENTPOSITION, (uptr_t)cur_line, 0);
            sci_msg(sci, SCI_SETSEL, (uptr_t)new_pos, (sptr_t)new_pos);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Tab switch                                                          */
/* ------------------------------------------------------------------ */

static void on_switch_page(GtkNotebook *nb, GtkWidget *page,
                           guint page_num, gpointer data)
{
    (void)nb; (void)data; (void)page_num;
    statusbar_update_from_sci(page);
    statusbar_set_language(lexer_display_name(
        (const char *)g_object_get_data(G_OBJECT(page), "npp-lang")));
    update_window_title();
    findreplace_set_sci(page);
    toolbar_sync_toggles(page);
}

/* ------------------------------------------------------------------ */
/* "Ask to save" dialog                                               */
/* ------------------------------------------------------------------ */

/* Returns TRUE if caller may proceed (saved or discarded), FALSE if cancelled */
static gboolean ask_save(NppDoc *doc)
{
    if (!doc->modified) return TRUE;

    const char *name = doc->filepath
        ? g_path_get_basename(doc->filepath)
        : "this document";

    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(s_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        T("msg.Reload.message", "Save changes to \"%s\"?"), name);

    gtk_dialog_add_button(GTK_DIALOG(dlg), TM("cmd.41004", "Close _Without Saving"), GTK_RESPONSE_NO);
    gtk_dialog_add_button(GTK_DIALOG(dlg), TM("dlg.Find.2",  "_Cancel"),             GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dlg), TM("cmd.41006",   "_Save"),               GTK_RESPONSE_YES);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_YES);

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (resp == GTK_RESPONSE_YES)  return editor_save();
    if (resp == GTK_RESPONSE_NO)   return TRUE;
    return FALSE; /* cancel */
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

GtkWidget *editor_init(GtkWidget *window)
{
    stylestore_init(NULL);
    s_window   = window;
    s_notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(s_notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(s_notebook), FALSE);
    g_signal_connect(s_notebook, "switch-page", G_CALLBACK(on_switch_page), NULL);
    editor_new_doc();
    return s_notebook;
}

GtkWidget *editor_get_notebook(void) { return s_notebook; }
int        editor_page_count(void)   { return gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook)); }
int        editor_current_page(void) { return gtk_notebook_get_current_page(GTK_NOTEBOOK(s_notebook)); }

NppDoc *editor_doc_at(int page)
{
    GtkWidget *sci = sci_of_page(page);
    return sci ? doc_of_sci(sci) : NULL;
}

NppDoc *editor_current_doc(void)
{
    return editor_doc_at(editor_current_page());
}

sptr_t editor_send(unsigned int msg, uptr_t wp, sptr_t lp)
{
    NppDoc *doc = editor_current_doc();
    return doc ? sci_msg(doc->sci, msg, wp, lp) : 0;
}

void editor_new_doc(void)
{
    s_new_count++;
    NppDoc *doc = g_new0(NppDoc, 1);
    doc->new_index  = s_new_count;
    doc->encoding   = g_strdup(g_prefs.default_encoding);

    GtkWidget *sci = scintilla_new();
    doc->sci = sci;
    g_object_set_data(G_OBJECT(sci), "npp-doc", doc);
    setup_sci(sci);
    sci_msg(sci, SCI_SETEOLMODE, (uptr_t)g_prefs.default_eol, 0);
    g_signal_connect(sci, "sci-notify", G_CALLBACK(on_sci_notify), NULL);

    GtkWidget *label = make_tab_label(doc, sci);
    int page = gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook));
    gtk_notebook_append_page(GTK_NOTEBOOK(s_notebook), sci, label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(s_notebook), sci, TRUE);
    gtk_widget_show_all(s_notebook);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(s_notebook), page);
}

gboolean editor_open_path(const char *path)
{
    /* Check if already open — switch to it */
    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook));
    for (int i = 0; i < n; i++) {
        NppDoc *d = editor_doc_at(i);
        if (d && d->filepath && strcmp(d->filepath, path) == 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(s_notebook), i);
            return TRUE;
        }
    }

    gchar   *contents = NULL;
    gsize    len      = 0;
    GError  *err      = NULL;
    if (!g_file_get_contents(path, &contents, &len, &err)) {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(s_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            T("msg.OpenFileError.message", "Cannot open file:\n%s"), err->message);
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        g_error_free(err);
        return FALSE;
    }

    /* reuse current tab if it's an untouched "new N" */
    NppDoc *cur = editor_current_doc();
    int page;
    GtkWidget *sci;
    if (cur && !cur->filepath && !cur->modified &&
        sci_msg(cur->sci, SCI_GETLENGTH, 0, 0) == 0) {
        page = editor_current_page();
        sci  = cur->sci;
        g_free(cur->filepath);
        cur->filepath   = g_strdup(path);
        cur->new_index  = 0;
    } else {
        s_new_count++;
        NppDoc *doc = g_new0(NppDoc, 1);
        doc->filepath = g_strdup(path);
        sci = scintilla_new();
        doc->sci = sci;
        g_object_set_data(G_OBJECT(sci), "npp-doc", doc);
        setup_sci(sci);
        g_signal_connect(sci, "sci-notify", G_CALLBACK(on_sci_notify), NULL);
        GtkWidget *label = make_tab_label(doc, sci);
        page = gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook));
        gtk_notebook_append_page(GTK_NOTEBOOK(s_notebook), sci, label);
        gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(s_notebook), sci, TRUE);
        gtk_widget_show_all(s_notebook);
        cur = doc;
    }

    const char *enc_name = encoding_detect((const guchar *)contents, len);
    gsize utf8_len = 0;
    char *utf8 = encoding_to_utf8(enc_name, (const guchar *)contents, len, &utf8_len);
    g_free(contents);
    g_free(cur->encoding);
    cur->encoding = g_strdup(enc_name);

    sci_msg(sci, SCI_SETTEXT, 0, (sptr_t)utf8);
    sci_msg(sci, SCI_SETSAVEPOINT, 0, 0);
    sci_msg(sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    sci_msg(sci, SCI_GOTOPOS, 0, 0);
    g_free(utf8);

    lexer_apply_from_path(sci, path);
    statusbar_set_language(lexer_display_name(
        (const char *)g_object_get_data(G_OBJECT(sci), "npp-lang")));

    refresh_tab_label(page);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(s_notebook), page);
    update_window_title();
    statusbar_update_from_sci(sci);
    findreplace_set_sci(sci);
    main_recent_file_add(path);
    return TRUE;
}

gboolean editor_open_dialog(void)
{
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        T("cmd.41002", "Open File"), GTK_WINDOW(s_window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        TM("dlg.Find.2",  "_Cancel"), GTK_RESPONSE_CANCEL,
        TM("cmd.41002",   "_Open"),   GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);

    gboolean opened = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
        for (GSList *f = files; f; f = f->next) {
            if (editor_open_path((char *)f->data)) opened = TRUE;
            g_free(f->data);
        }
        g_slist_free(files);
    }
    gtk_widget_destroy(dlg);
    return opened;
}

static gboolean save_doc_to_path(NppDoc *doc, const char *path)
{
    sptr_t  utf8_len = sci_msg(doc->sci, SCI_GETLENGTH, 0, 0);
    gchar  *utf8     = g_new(gchar, utf8_len + 1);
    sci_msg(doc->sci, SCI_GETTEXT, (uptr_t)(utf8_len + 1), (sptr_t)utf8);

    const char *enc = doc->encoding ? doc->encoding : "UTF-8";
    gsize  out_len = 0;
    guchar *buf    = encoding_from_utf8(enc, utf8, (gsize)utf8_len, &out_len);
    g_free(utf8);

    GError *err = NULL;
    if (!g_file_set_contents(path, (const gchar *)buf, (gssize)out_len, &err)) {
        g_free(buf);
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(s_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            T("msg.SaveFileError.message", "Cannot save file:\n%s"), err->message);
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        g_error_free(err);
        return FALSE;
    }
    g_free(buf);
    sci_msg(doc->sci, SCI_SETSAVEPOINT, 0, 0);
    return TRUE;
}

gboolean editor_save(void)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return FALSE;
    if (!doc->filepath) return editor_save_as_dialog();
    return save_doc_to_path(doc, doc->filepath);
}

gboolean editor_save_at(int page)
{
    NppDoc *doc = editor_doc_at(page);
    if (!doc) return FALSE;
    if (!doc->filepath) {
        /* Switch to that page, show save-as dialog */
        gtk_notebook_set_current_page(GTK_NOTEBOOK(editor_get_notebook()), page);
        return editor_save_as_dialog();
    }
    return save_doc_to_path(doc, doc->filepath);
}

gboolean editor_save_as_dialog(void)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return FALSE;

    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        T("cmd.41008", "Save File As"), GTK_WINDOW(s_window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        TM("dlg.Find.2",  "_Cancel"), GTK_RESPONSE_CANCEL,
        TM("cmd.41006",   "_Save"),   GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    if (doc->filepath)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), doc->filepath);

    gboolean saved = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (save_doc_to_path(doc, path)) {
            g_free(doc->filepath);
            doc->filepath  = path;
            doc->new_index = 0;
            refresh_tab_label(editor_current_page());
            update_window_title();
            main_recent_file_add(path);
            saved = TRUE;
        } else {
            g_free(path);
        }
    }
    gtk_widget_destroy(dlg);
    return saved;
}

gboolean editor_close_page(int page)
{
    if (page < 0) page = editor_current_page();
    GtkWidget *sci = sci_of_page(page);
    if (!sci) return FALSE;
    NppDoc *doc = doc_of_sci(sci);

    if (!ask_save(doc)) return FALSE;

    gtk_notebook_remove_page(GTK_NOTEBOOK(s_notebook), page);
    g_free(doc->filepath);
    g_free(doc->encoding);
    g_free(doc);

    /* keep at least one tab open */
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook)) == 0)
        editor_new_doc();

    update_window_title();
    return TRUE;
}

void editor_close_all_quit(GApplication *app)
{
    while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook)) > 0) {
        NppDoc *doc = editor_doc_at(0);
        if (!doc) break;
        if (!ask_save(doc)) return; /* user cancelled */
        GtkWidget *sci = sci_of_page(0);
        gtk_notebook_remove_page(GTK_NOTEBOOK(s_notebook), 0);
        g_free(doc->filepath);
        g_free(doc->encoding);
        g_free(doc);
        (void)sci;
    }
    g_application_quit(app);
}

/* ------------------------------------------------------------------ */
/* Apply preferences to all open editors                              */
/* ------------------------------------------------------------------ */

void editor_apply_prefs(void)
{
    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook));
    for (int i = 0; i < n; i++) {
        GtkWidget *sci = gtk_notebook_get_nth_page(GTK_NOTEBOOK(s_notebook), i);
        if (!sci) continue;
        sci_msg(sci, SCI_SETTABWIDTH,        (uptr_t)g_prefs.tab_width,  0);
        sci_msg(sci, SCI_SETUSETABS,         (uptr_t)g_prefs.use_tabs,   0);
        sci_msg(sci, SCI_SETCARETLINEVISIBLE,(uptr_t)g_prefs.highlight_current_line, 0);
        sci_msg(sci, SCI_SETCARETWIDTH,      (uptr_t)g_prefs.caret_width, 0);
        sci_msg(sci, SCI_SETCARETPERIOD,     (uptr_t)g_prefs.caret_blink_rate, 0);
        sci_msg(sci, SCI_SETENDATLASTLINE,   g_prefs.scroll_beyond_last_line ? 0 : 1, 0);
    }
    statusbar_update_from_sci(
        gtk_notebook_get_nth_page(GTK_NOTEBOOK(s_notebook),
            gtk_notebook_get_current_page(GTK_NOTEBOOK(s_notebook))));
}

/* Called from prefs.c to refresh all window titles */
void main_refresh_title(void);

/* ------------------------------------------------------------------ */
/* Edit operations                                                     */
/* ------------------------------------------------------------------ */

void editor_undo(void)       { editor_send(SCI_UNDO,       0, 0); }
void editor_redo(void)       { editor_send(SCI_REDO,       0, 0); }
void editor_cut(void)        { editor_send(SCI_CUT,        0, 0); }
void editor_copy(void)       { editor_send(SCI_COPY,       0, 0); }
void editor_paste(void)      { editor_send(SCI_PASTE,      0, 0); }
void editor_select_all(void) { editor_send(SCI_SELECTALL,  0, 0); }

void editor_reapply_styles(void)
{
    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook));
    for (int i = 0; i < n; i++) {
        GtkWidget *sci = sci_of_page(i);
        if (!sci) continue;
        const char *lang = (const char *)g_object_get_data(G_OBJECT(sci), "npp-lang");
        stylestore_apply_default(sci);
        sci_msg(sci, SCI_STYLECLEARALL, 0, 0);
        stylestore_apply_global(sci);
        if (lang && *lang)
            stylestore_apply_lexer(sci, lang);
    }
}

void editor_goto_line_dialog(void)
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        T("dlg.GoToLine.title", "Go To Line"), GTK_WINDOW(s_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        TM("dlg.Find.2",       "_Cancel"), GTK_RESPONSE_CANCEL,
        TM("dlg.GoToLine.1",   "_Go"),     GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *hbox    = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox, 12);
    gtk_widget_set_margin_top(hbox, 8);
    gtk_widget_set_margin_bottom(hbox, 8);
    gtk_box_pack_start(GTK_BOX(content), hbox, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(T("dlg.GoToLine.2007", "Line number:")), FALSE, FALSE, 0);

    /* upper bound = current line count */
    sptr_t lines = editor_send(SCI_LINEFROMPOSITION,
        (uptr_t)editor_send(SCI_GETLENGTH, 0, 0), 0) + 1;
    GtkAdjustment *adj = gtk_adjustment_new(1, 1, (gdouble)lines, 1, 10, 0);
    GtkWidget *spin = gtk_spin_button_new(adj, 1, 0);
    gtk_entry_set_activates_default(GTK_ENTRY(spin), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), spin, TRUE, TRUE, 0);

    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        int line = (int)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)) - 1;
        editor_send(SCI_GOTOLINE, (uptr_t)line, 0);
    }
    gtk_widget_destroy(dlg);
}

void editor_open_and_goto(const char *path, int line)
{
    editor_open_path(path);
    NppDoc *doc = editor_current_doc();
    if (doc && line > 0) {
        sci_msg(doc->sci, SCI_GOTOLINE,    (uptr_t)(line - 1), 0);
        sci_msg(doc->sci, SCI_SCROLLCARET, 0, 0);
        gtk_widget_grab_focus(doc->sci);
    }
}
