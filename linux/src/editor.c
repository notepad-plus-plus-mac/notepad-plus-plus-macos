#include "editor.h"
#include "backup.h"
#include "encoding.h"
#include "prefs.h"
#include "statusbar.h"
#include "lexer.h"
#include "findreplace.h"
#include "toolbar.h"
#include "stylestore.h"
#include "i18n.h"
#include "autocomplete.h"
#include "gitgutter.h"
#include "changehistory.h"
#include "macro.h"
#include "funclist.h"
#include "docmap.h"
#include "spell.h"
#include "plugin.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static GtkWidget *s_notebook;
static GtkWidget *s_window;
static int        s_new_count;

/* Persistent file-chooser singletons — never destroyed to avoid GIO-in-background crashes */
static GtkWidget *s_open_dlg     = NULL;
static GtkWidget *s_saveas_dlg   = NULL;
static GtkWidget *s_savecopy_dlg = NULL;

/* TRUE while editor_open_path / reload_doc_from_disk is populating a Scintilla
 * widget via SCI_SETTEXT.  Suppresses changehistory_on_modified so that the
 * ~N*2 RedrawSelMargin / InvalidateRect calls emitted for each inserted line
 * do not overwhelm GTK's damage-region tracking (which causes a blank view on
 * large files). */
static gboolean s_loading_file = FALSE;

/* Incremental search bar */
#define INCR_INDICATOR 9
static GtkWidget    *s_editor_container;
static GtkWidget    *s_search_bar;
static GtkWidget    *s_search_entry;
static GtkWidget    *s_search_case;
static Sci_Position  s_incr_match_end = -1;

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

static void refresh_tab_label(int page);
static void update_window_title(void);

/* ------------------------------------------------------------------ */
/* File change detection                                               */
/* ------------------------------------------------------------------ */

static void reload_doc_from_disk(NppDoc *doc)
{
    gchar  *contents = NULL;
    gsize   len      = 0;
    GError *err      = NULL;
    if (!g_file_get_contents(doc->filepath, &contents, &len, &err)) {
        g_error_free(err);
        return;
    }
    const char *enc_name = encoding_detect((const guchar *)contents, len);
    gsize utf8_len = 0;
    char *utf8 = encoding_to_utf8(enc_name, (const guchar *)contents, len, &utf8_len);
    g_free(contents);
    g_free(doc->encoding);
    doc->encoding = g_strdup(enc_name);

    Sci_Position saved_pos = (Sci_Position)sci_msg(doc->sci, SCI_GETCURRENTPOS, 0, 0);
    s_loading_file = TRUE;
    sci_msg(doc->sci, SCI_SETTEXT, 0, (sptr_t)utf8);
    sci_msg(doc->sci, SCI_SETSAVEPOINT, 0, 0);
    sci_msg(doc->sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    s_loading_file = FALSE;
    sci_msg(doc->sci, SCI_GOTOPOS, (uptr_t)saved_pos, 0);
    g_free(utf8);

    lexer_apply_from_path(doc->sci, doc->filepath);
    gitgutter_update(doc->sci, doc->filepath);

    int page = gtk_notebook_page_num(GTK_NOTEBOOK(s_notebook), doc->sci);
    refresh_tab_label(page);
    statusbar_update_from_sci(doc->sci);
}

static void on_file_changed(GFileMonitor *mon, GFile *file, GFile *other,
                             GFileMonitorEvent event, gpointer user_data)
{
    (void)mon; (void)file; (void)other;
    NppDoc *doc = user_data;

    if (event != G_FILE_MONITOR_EVENT_CHANGED &&
        event != G_FILE_MONITOR_EVENT_CREATED)
        return;

    if (doc->ignore_next_change) {
        doc->ignore_next_change = FALSE;
        return;
    }

    /* Monitoring mode: reload silently without prompting */
    if (doc->monitoring) {
        reload_doc_from_disk(doc);
        return;
    }

    /* Only prompt when the window is focused or the tab is visible */
    const char *basename = g_path_get_basename(doc->filepath);
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(s_window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "The file \"%s\" has been changed externally.\nReload it?", basename);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "_Reload", GTK_RESPONSE_YES);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "_Keep current", GTK_RESPONSE_NO);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_YES);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_YES)
        reload_doc_from_disk(doc);
    gtk_widget_destroy(dlg);
}

static void filewatch_start(NppDoc *doc)
{
    if (!doc->filepath) return;
    if (doc->file_monitor) {
        g_file_monitor_cancel(doc->file_monitor);
        g_object_unref(doc->file_monitor);
    }
    GFile *gf = g_file_new_for_path(doc->filepath);
    GError *err = NULL;
    doc->file_monitor = g_file_monitor_file(gf, G_FILE_MONITOR_NONE, NULL, &err);
    g_object_unref(gf);
    if (err) { g_error_free(err); doc->file_monitor = NULL; return; }
    g_signal_connect(doc->file_monitor, "changed", G_CALLBACK(on_file_changed), doc);
}

static void filewatch_stop(NppDoc *doc)
{
    if (doc->file_monitor) {
        g_file_monitor_cancel(doc->file_monitor);
        g_object_unref(doc->file_monitor);
        doc->file_monitor = NULL;
    }
}

static gboolean on_sci_button_press(GtkWidget *w, GdkEventButton *ev, gpointer d)
{
    (void)d;
    if (ev->type == GDK_BUTTON_PRESS && ev->button == 3) {
        GtkWidget *menu = gtk_menu_new();
        spell_populate_context_menu(w, menu, (int)ev->x, (int)ev->y);
        if (gtk_container_get_children(GTK_CONTAINER(menu))) {
            gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)ev);
            return TRUE;  /* consumed — suppress Scintilla's own right-click */
        }
        gtk_widget_destroy(menu);
    }
    return FALSE;
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
    sci_msg(sci, SCI_SETMARGINMASKN,     1, (sptr_t)(1 << SC_MARKNUM_BOOKMARK));
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_BOOKMARK, SC_MARK_BOOKMARK);
    sci_msg(sci, SCI_MARKERSETFORE, SC_MARKNUM_BOOKMARK, 0x0000FF); /* blue */
    sci_msg(sci, SCI_MARKERSETBACK, SC_MARKNUM_BOOKMARK, 0x0080FF);
    /* Margin 2: fold — box +/- tree markers */
    sci_msg(sci, SCI_SETMARGINTYPE,      2, SC_MARGIN_SYMBOL);
    sci_msg(sci, SCI_SETMARGINSENSITIVE, 2, 1);
    sci_msg(sci, SCI_SETMARGINWIDTHN,    2, 16);
    sci_msg(sci, SCI_SETMARGINMASKN,     2, (sptr_t)SC_MASK_FOLDERS);
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDER,       SC_MARK_BOXPLUS);
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN,   SC_MARK_BOXMINUS);
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND,    SC_MARK_BOXPLUSCONNECTED);
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID,SC_MARK_BOXMINUSCONNECTED);
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB,    SC_MARK_VLINE);
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL,SC_MARK_TCORNER);
    sci_msg(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL,   SC_MARK_LCORNER);
    /* Show a line below contracted folds */
    sci_msg(sci, SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED, 0);
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
    /* Indicator 9: incremental search highlight (green) */
    sci_msg(sci, SCI_INDICSETSTYLE, INCR_INDICATOR, INDIC_ROUNDBOX);
    sci_msg(sci, SCI_INDICSETFORE,  INCR_INDICATOR, 0x00CC44); /* BGR green */
    sci_msg(sci, SCI_INDICSETALPHA, INCR_INDICATOR, 130);
    sci_msg(sci, SCI_SETVIRTUALSPACEOPTIONS,
            SCVS_RECTANGULARSELECTION | SCVS_USERACCESSIBLE, 0);
    sci_msg(sci, SCI_SETMULTIPLESELECTION,         1, 0);
    sci_msg(sci, SCI_SETADDITIONALSELECTIONTYPING, 1, 0);
    sci_msg(sci, SCI_SETMULTIPASTE,  SC_MULTIPASTE_EACH, 0);
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
    /* Apply correct margin widths now that fonts/styles are set */
    main_apply_view_symbols(sci);
    autocomplete_setup(sci);
    gitgutter_setup(sci);
    changehistory_setup(sci);
    spell_on_sci_created(sci);
    gtk_widget_add_events(sci, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(sci, "button-press-event",
                     G_CALLBACK(on_sci_button_press), NULL);
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
        snprintf(buf, sizeof(buf), "%s%s — Notetux++", mod, name);
    } else {
        snprintf(buf, sizeof(buf), "%snew %d — Notetux++", mod, doc->new_index);
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

    plugin_notify_all(n);

    unsigned int code = n->nmhdr.code;

    if (code == SCN_SAVEPOINTREACHED) {
        doc->modified = FALSE;
        backup_clean(doc);
        changehistory_on_save(sci);
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
        if (gtk_notebook_get_nth_page(GTK_NOTEBOOK(s_notebook), cur) == sci) {
            statusbar_update_from_sci(sci);
            docmap_sync_scroll(sci);
        }

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
        /* SCN_MARGINCLICK sets position (line start), not line — derive it */
        int line = (int)sci_msg(sci, SCI_LINEFROMPOSITION, (uptr_t)n->position, 0);
        if (n->margin == 1) {
            main_toggle_bookmark_at_line(sci, line);
        } else if (n->margin == 2) {
            int lvl = (int)sci_msg(sci, SCI_GETFOLDLEVEL, (uptr_t)line, 0);
            if (lvl & SC_FOLDLEVELHEADERFLAG)
                sci_msg(sci, SCI_TOGGLEFOLD, (uptr_t)line, 0);
        }
    } else if (code == SCN_CHARADDED) {
        autocomplete_on_char_added(sci, n->ch);
        if (g_prefs.auto_indent != AUTO_INDENT_NONE && (n->ch == '\n' || n->ch == '\r')) {
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
    } else if (code == SCN_MODIFIED &&
               (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
        if (!s_loading_file) {
            Sci_Position mod_line = (Sci_Position)sci_msg(sci, SCI_LINEFROMPOSITION,
                (uptr_t)n->position, 0);
            changehistory_on_modified(sci, mod_line, n->linesAdded);
            if (doc->filepath)
                gitgutter_update(sci, doc->filepath);
        }
        funclist_schedule_update(sci);
        spell_schedule_check(sci);
    } else if (code == SCN_MACRORECORD) {
        macro_on_record((unsigned int)n->message, n->wParam, n->lParam);
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
/* Incremental search helpers                                          */
/* ------------------------------------------------------------------ */

static void incr_search_do(void)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    const char *needle = gtk_entry_get_text(GTK_ENTRY(s_search_entry));

    sptr_t doclen = sci_msg(doc->sci, SCI_GETLENGTH, 0, 0);
    sci_msg(doc->sci, SCI_SETINDICATORCURRENT, INCR_INDICATOR, 0);
    sci_msg(doc->sci, SCI_INDICATORCLEARRANGE, 0, doclen);
    s_incr_match_end = -1;

    if (!needle || !*needle) return;

    gboolean cs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s_search_case));
    sci_msg(doc->sci, SCI_SETSEARCHFLAGS, cs ? SCFIND_MATCHCASE : 0, 0);

    Sci_Position caret = (Sci_Position)sci_msg(doc->sci, SCI_GETCURRENTPOS, 0, 0);
    Sci_Position first_match      = -1;
    Sci_Position first_after_caret = -1;
    Sci_Position first_end         = -1;
    gsize needle_len = strlen(needle);

    for (Sci_Position pos = 0; pos < doclen; ) {
        sci_msg(doc->sci, SCI_SETTARGETSTART, (uptr_t)pos, 0);
        sci_msg(doc->sci, SCI_SETTARGETEND,   (uptr_t)doclen, 0);
        sptr_t found = sci_msg(doc->sci, SCI_SEARCHINTARGET, (uptr_t)needle_len, (sptr_t)needle);
        if (found < 0) break;
        Sci_Position end = (Sci_Position)sci_msg(doc->sci, SCI_GETTARGETEND, 0, 0);
        sci_msg(doc->sci, SCI_INDICATORFILLRANGE, (uptr_t)found, (sptr_t)(end - found));
        if (first_match < 0) { first_match = found; first_end = end; }
        if (first_after_caret < 0 && found >= caret) { first_after_caret = found; first_end = end; }
        pos = (end > pos) ? end : pos + 1;
    }

    Sci_Position goto_pos = (first_after_caret >= 0) ? first_after_caret
                          : (first_match      >= 0) ? first_match : -1;
    if (goto_pos >= 0) {
        s_incr_match_end = first_end;
        sci_msg(doc->sci, SCI_GOTOPOS, (uptr_t)goto_pos, 0);
        sci_msg(doc->sci, SCI_SCROLLCARET, 0, 0);
    }
}

static void incr_search_next(void)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return;
    const char *needle = gtk_entry_get_text(GTK_ENTRY(s_search_entry));
    if (!needle || !*needle) return;

    gboolean cs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s_search_case));
    sci_msg(doc->sci, SCI_SETSEARCHFLAGS, cs ? SCFIND_MATCHCASE : 0, 0);
    sptr_t doclen = sci_msg(doc->sci, SCI_GETLENGTH, 0, 0);
    gsize needle_len = strlen(needle);

    Sci_Position from = (s_incr_match_end >= 0) ? s_incr_match_end : 0;
    sci_msg(doc->sci, SCI_SETTARGETSTART, (uptr_t)from, 0);
    sci_msg(doc->sci, SCI_SETTARGETEND,   (uptr_t)doclen, 0);
    sptr_t found = sci_msg(doc->sci, SCI_SEARCHINTARGET, (uptr_t)needle_len, (sptr_t)needle);
    if (found < 0) {
        /* wrap around */
        sci_msg(doc->sci, SCI_SETTARGETSTART, 0, 0);
        sci_msg(doc->sci, SCI_SETTARGETEND,   (uptr_t)doclen, 0);
        found = sci_msg(doc->sci, SCI_SEARCHINTARGET, (uptr_t)needle_len, (sptr_t)needle);
    }
    if (found >= 0) {
        s_incr_match_end = (Sci_Position)sci_msg(doc->sci, SCI_GETTARGETEND, 0, 0);
        sci_msg(doc->sci, SCI_GOTOPOS, (uptr_t)found, 0);
        sci_msg(doc->sci, SCI_SCROLLCARET, 0, 0);
    }
}

static gboolean on_search_entry_key(GtkWidget *w, GdkEventKey *ev, gpointer d)
{
    (void)w; (void)d;
    if (ev->keyval == GDK_KEY_Escape) { editor_incr_search_close(); return TRUE; }
    if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter)
        { incr_search_next(); return TRUE; }
    return FALSE;
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

    /* Incremental search bar — hidden by default, shown via Ctrl+I */
    s_search_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *lbl  = gtk_label_new("Find:");
    s_search_entry  = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(s_search_entry), 30);
    s_search_case   = gtk_check_button_new_with_label("Match case");
    GtkWidget *close_btn = gtk_button_new_with_label("✕");
    gtk_widget_set_tooltip_text(close_btn, "Close (Escape)");
    gtk_box_pack_start(GTK_BOX(s_search_bar), lbl,         FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(s_search_bar), s_search_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(s_search_bar), s_search_case,  FALSE, FALSE, 4);
    gtk_box_pack_end  (GTK_BOX(s_search_bar), close_btn,      FALSE, FALSE, 4);
    g_signal_connect_swapped(s_search_entry, "changed",        G_CALLBACK(incr_search_do),  NULL);
    g_signal_connect(        s_search_entry, "key-press-event",G_CALLBACK(on_search_entry_key), NULL);
    g_signal_connect_swapped(s_search_case,  "toggled",        G_CALLBACK(incr_search_do),  NULL);
    g_signal_connect_swapped(close_btn,      "clicked",        G_CALLBACK(editor_incr_search_close), NULL);

    s_editor_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(s_editor_container), s_notebook,    TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(s_editor_container), s_search_bar,  FALSE, FALSE, 0);
    return s_editor_container;
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
    main_doclist_refresh();
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
    gboolean reuse = FALSE;
    if (cur && !cur->filepath && !cur->modified &&
        sci_msg(cur->sci, SCI_GETLENGTH, 0, 0) == 0) {
        page = editor_current_page();
        sci  = cur->sci;
        g_free(cur->filepath);
        cur->filepath   = g_strdup(path);
        cur->new_index  = 0;
        reuse = TRUE;
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
        /* Switch to the new tab NOW so the widget is mapped with real dimensions
         * before SCI_SETTEXT / SCI_GOTOPOS; without this, Scintilla calculates
         * the first-visible-line against a zero-height viewport and the content
         * appears blank when the tab is later shown. */
        gtk_notebook_set_current_page(GTK_NOTEBOOK(s_notebook), page);
        cur = doc;
    }

    const char *enc_name = encoding_detect((const guchar *)contents, len);
    gsize utf8_len = 0;
    char *utf8 = encoding_to_utf8(enc_name, (const guchar *)contents, len, &utf8_len);
    g_free(contents);
    g_free(cur->encoding);
    cur->encoding = g_strdup(enc_name);

    if (reuse) {
        /* The reuse path skips gtk_notebook_set_current_page, so Scintilla
         * never gets a fresh size-allocate before we load the text.  Send
         * one explicitly so its internal linesOnScreen / scroll-range state
         * is current; without this the widget renders blank after SCI_SETTEXT. */
        GtkAllocation alloc;
        gtk_widget_get_allocation(sci, &alloc);
        gtk_widget_size_allocate(sci, &alloc);
    }

    s_loading_file = TRUE;
    sci_msg(sci, SCI_SETTEXT, 0, (sptr_t)utf8);
    sci_msg(sci, SCI_SETSAVEPOINT, 0, 0);
    sci_msg(sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    s_loading_file = FALSE;
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
    main_doclist_refresh();
    gitgutter_update(sci, path);
    filewatch_start(cur);
    return TRUE;
}

gboolean editor_open_dialog(void)
{
    if (!s_open_dlg) {
        s_open_dlg = gtk_file_chooser_dialog_new(
            T("cmd.41002", "Open File"), GTK_WINDOW(s_window),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            TM("dlg.Find.2", "_Cancel"), GTK_RESPONSE_CANCEL,
            TM("cmd.41002",  "_Open"),   GTK_RESPONSE_ACCEPT,
            NULL);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(s_open_dlg), TRUE);
        g_signal_connect(s_open_dlg, "delete-event",
                         G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    }

    gboolean opened = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(s_open_dlg)) == GTK_RESPONSE_ACCEPT) {
        GSList *paths = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(s_open_dlg));
        if (!paths) {
            /* Fallback for portal/Wayland: use URI list */
            GSList *uris = gtk_file_chooser_get_uris(GTK_FILE_CHOOSER(s_open_dlg));
            for (GSList *u = uris; u; u = u->next) {
                char *p = g_filename_from_uri((char *)u->data, NULL, NULL);
                if (p) {
                    if (editor_open_path(p)) opened = TRUE;
                    g_free(p);
                }
                g_free(u->data);
            }
            g_slist_free(uris);
        } else {
            for (GSList *f = paths; f; f = f->next) {
                if (editor_open_path((char *)f->data)) opened = TRUE;
                g_free(f->data);
            }
            g_slist_free(paths);
        }
    }
    gtk_widget_hide(s_open_dlg);
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

    doc->ignore_next_change = TRUE;
    GError *err = NULL;
    if (!g_file_set_contents(path, (const gchar *)buf, (gssize)out_len, &err)) {
        doc->ignore_next_change = FALSE;
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
    gitgutter_update(doc->sci, path);
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

    if (!s_saveas_dlg) {
        s_saveas_dlg = gtk_file_chooser_dialog_new(
            T("cmd.41008", "Save File As"), GTK_WINDOW(s_window),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            TM("dlg.Find.2", "_Cancel"), GTK_RESPONSE_CANCEL,
            TM("cmd.41006",  "_Save"),   GTK_RESPONSE_ACCEPT,
            NULL);
        gtk_file_chooser_set_do_overwrite_confirmation(
            GTK_FILE_CHOOSER(s_saveas_dlg), TRUE);
        g_signal_connect(s_saveas_dlg, "delete-event",
                         G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    }
    if (doc->filepath)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(s_saveas_dlg), doc->filepath);

    gboolean saved = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(s_saveas_dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(s_saveas_dlg));
        if (path && save_doc_to_path(doc, path)) {
            g_free(doc->filepath);
            doc->filepath  = path;
            doc->new_index = 0;
            filewatch_start(doc);
            refresh_tab_label(editor_current_page());
            update_window_title();
            main_recent_file_add(path);
            main_doclist_refresh();
            saved = TRUE;
        } else {
            g_free(path);
        }
    }
    gtk_widget_hide(s_saveas_dlg);
    return saved;
}

gboolean editor_close_page(int page)
{
    if (page < 0) page = editor_current_page();
    GtkWidget *sci = sci_of_page(page);
    if (!sci) return FALSE;
    NppDoc *doc = doc_of_sci(sci);

    if (!ask_save(doc)) return FALSE;

    filewatch_stop(doc);
    backup_clean(doc);
    gtk_notebook_remove_page(GTK_NOTEBOOK(s_notebook), page);
    g_free(doc->filepath);
    g_free(doc->encoding);
    g_free(doc);

    /* keep at least one tab open */
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook)) == 0)
        editor_new_doc();

    update_window_title();
    main_doclist_refresh();
    return TRUE;
}

gboolean editor_save_all(void)
{
    gboolean ok = TRUE;
    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook));
    for (int p = 0; p < n; p++) {
        NppDoc *doc = editor_doc_at(p);
        if (doc && doc->modified)
            if (!editor_save_at(p)) ok = FALSE;
    }
    return ok;
}

void editor_reload_current(void)
{
    NppDoc *doc = editor_current_doc();
    if (!doc || !doc->filepath) return;
    reload_doc_from_disk(doc);
}

gboolean editor_close_all_but_current(void)
{
    int cur = editor_current_page();
    /* close from right */
    int n;
    while ((n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook))) > 1) {
        int target = (n - 1 == cur) ? n - 2 : n - 1;
        if (target < 0) break;
        if (!editor_close_page(target)) return FALSE;
        if (target < cur) cur--;
    }
    return TRUE;
}

void editor_close_all_quit(GApplication *app)
{
    while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(s_notebook)) > 0) {
        NppDoc *doc = editor_doc_at(0);
        if (!doc) break;
        if (!ask_save(doc)) return; /* user cancelled */
        filewatch_stop(doc);
        backup_clean(doc);
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

gboolean editor_save_copy_as(void)
{
    NppDoc *doc = editor_current_doc();
    if (!doc) return FALSE;

    if (!s_savecopy_dlg) {
        s_savecopy_dlg = gtk_file_chooser_dialog_new(
            "Save a Copy As", GTK_WINDOW(s_window),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel",     GTK_RESPONSE_CANCEL,
            "_Save Copy",  GTK_RESPONSE_ACCEPT,
            NULL);
        gtk_file_chooser_set_do_overwrite_confirmation(
            GTK_FILE_CHOOSER(s_savecopy_dlg), TRUE);
        g_signal_connect(s_savecopy_dlg, "delete-event",
                         G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    }
    if (doc->filepath)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(s_savecopy_dlg), doc->filepath);

    gboolean saved = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(s_savecopy_dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(s_savecopy_dlg));
        /* Write to path without changing doc->filepath or save-point */
        sptr_t utf8_len = sci_msg(doc->sci, SCI_GETLENGTH, 0, 0);
        gchar *utf8 = g_new(gchar, utf8_len + 1);
        sci_msg(doc->sci, SCI_GETTEXT, (uptr_t)(utf8_len + 1), (sptr_t)utf8);
        const char *enc = doc->encoding ? doc->encoding : "UTF-8";
        gsize out_len = 0;
        guchar *buf = encoding_from_utf8(enc, utf8, (gsize)utf8_len, &out_len);
        g_free(utf8);
        GError *err = NULL;
        if (g_file_set_contents(path, (const gchar *)buf, (gssize)out_len, &err)) {
            saved = TRUE;
        } else {
            GtkWidget *edlg = gtk_message_dialog_new(GTK_WINDOW(s_window),
                GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "Cannot save copy:\n%s", err->message);
            gtk_dialog_run(GTK_DIALOG(edlg));
            gtk_widget_destroy(edlg);
            g_error_free(err);
        }
        g_free(buf);
        g_free(path);
    }
    gtk_widget_hide(s_savecopy_dlg);
    return saved;
}

gboolean editor_rename(void)
{
    NppDoc *doc = editor_current_doc();
    if (!doc || !doc->filepath) return FALSE;

    char *dir  = g_path_get_dirname(doc->filepath);
    char *base = g_path_get_basename(doc->filepath);

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Rename", GTK_WINDOW(s_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Rename", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *hbox    = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox, 12);
    gtk_widget_set_margin_top(hbox, 8);
    gtk_widget_set_margin_bottom(hbox, 8);
    gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("New name:"), FALSE, FALSE, 0);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), base);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 40);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

    gtk_widget_show_all(dlg);

    gboolean renamed = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        const char *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (new_name && *new_name && g_strcmp0(new_name, base) != 0) {
            char *new_path = g_build_filename(dir, new_name, NULL);
            if (rename(doc->filepath, new_path) == 0) {
                filewatch_stop(doc);
                g_free(doc->filepath);
                doc->filepath = new_path;
                filewatch_start(doc);
                refresh_tab_label(editor_current_page());
                update_window_title();
                main_recent_file_add(new_path);
                main_doclist_refresh();
                renamed = TRUE;
            } else {
                GtkWidget *edlg = gtk_message_dialog_new(GTK_WINDOW(s_window),
                    GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "Cannot rename file.");
                gtk_dialog_run(GTK_DIALOG(edlg));
                gtk_widget_destroy(edlg);
                g_free(new_path);
            }
        }
    }
    gtk_widget_destroy(dlg);
    g_free(dir);
    g_free(base);
    return renamed;
}

void editor_incr_search_show(void)
{
    gtk_widget_show(s_search_bar);
    gtk_widget_grab_focus(s_search_entry);
    incr_search_do();
}

void editor_incr_search_close(void)
{
    gtk_widget_hide(s_search_bar);
    NppDoc *doc = editor_current_doc();
    if (doc) {
        sptr_t doclen = sci_msg(doc->sci, SCI_GETLENGTH, 0, 0);
        sci_msg(doc->sci, SCI_SETINDICATORCURRENT, INCR_INDICATOR, 0);
        sci_msg(doc->sci, SCI_INDICATORCLEARRANGE, 0, doclen);
        gtk_widget_grab_focus(doc->sci);
    }
    s_incr_match_end = -1;
}
