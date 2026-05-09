#include "macro.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* SCI constants                                                       */
/* ------------------------------------------------------------------ */
#define SCI_STARTRECORD  3001
#define SCI_STOPRECORD   3002

/* Messages whose lParam is a NUL-terminated string pointer */
static const unsigned int k_string_msgs[] = {
    2003, /* SCI_INSERTTEXT  */
    2170, /* SCI_REPLACESEL  */
    2181, /* SCI_SETTEXT     */
    2001, /* SCI_ADDTEXT     */
    2282, /* SCI_APPENDTEXT  */
};
#define K_NSTR (sizeof(k_string_msgs) / sizeof(k_string_msgs[0]))

static gboolean lp_is_string(unsigned int msg)
{
    for (size_t i = 0; i < K_NSTR; i++)
        if (k_string_msgs[i] == msg) return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Storage                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    unsigned int msg;
    uptr_t       wp;
    sptr_t       lp;      /* integer value, or 0 when text != NULL */
    char        *text;    /* heap copy when lParam was a string */
} MacroStep;

#define MAX_STEPS 65536

static MacroStep  s_steps[MAX_STEPS];
static int        s_count      = 0;
static gboolean   s_recording  = FALSE;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

static sptr_t sci_msg(GtkWidget *sci, unsigned int m, uptr_t w, sptr_t l)
{
    return scintilla_send_message(SCINTILLA(sci), m, w, l);
}

void macro_start_recording(GtkWidget *sci)
{
    /* Clear previous macro */
    for (int i = 0; i < s_count; i++)
        g_free(s_steps[i].text);
    s_count     = 0;
    s_recording = TRUE;
    sci_msg(sci, SCI_STARTRECORD, 0, 0);
}

void macro_stop_recording(GtkWidget *sci)
{
    s_recording = FALSE;
    sci_msg(sci, SCI_STOPRECORD, 0, 0);
}

void macro_on_record(unsigned int msg, uptr_t wp, sptr_t lp)
{
    if (!s_recording || s_count >= MAX_STEPS) return;
    MacroStep *step = &s_steps[s_count++];
    step->msg  = msg;
    step->wp   = wp;
    step->text = NULL;
    if (lp_is_string(msg) && lp != 0) {
        step->text = g_strdup((const char *)lp);
        step->lp   = 0;
    } else {
        step->lp = lp;
    }
}

void macro_playback(GtkWidget *sci)
{
    if (s_recording || s_count == 0) return;
    sci_msg(sci, SCI_BEGINUNDOACTION, 0, 0);
    for (int i = 0; i < s_count; i++) {
        MacroStep *step = &s_steps[i];
        sptr_t lp = step->text ? (sptr_t)step->text : step->lp;
        sci_msg(sci, step->msg, step->wp, lp);
    }
    sci_msg(sci, SCI_ENDUNDOACTION, 0, 0);
}

void macro_playback_n(GtkWidget *sci, GtkWindow *parent)
{
    if (s_recording || s_count == 0) return;

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Run Macro Multiple Times",
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Run",    GTK_RESPONSE_OK,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *hbox    = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 12);
    GtkWidget *label   = gtk_label_new("Number of times:");
    GtkWidget *spin    = gtk_spin_button_new_with_range(1, 10000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
    gtk_entry_set_activates_default(GTK_ENTRY(spin), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), spin,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 0);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        int n = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
        gtk_widget_destroy(dlg);
        sci_msg(sci, SCI_BEGINUNDOACTION, 0, 0);
        for (int t = 0; t < n; t++) {
            for (int i = 0; i < s_count; i++) {
                MacroStep *step = &s_steps[i];
                sptr_t lp = step->text ? (sptr_t)step->text : step->lp;
                sci_msg(sci, step->msg, step->wp, lp);
            }
        }
        sci_msg(sci, SCI_ENDUNDOACTION, 0, 0);
    } else {
        gtk_widget_destroy(dlg);
    }
}

gboolean macro_is_recording(void) { return s_recording; }
gboolean macro_has_macro(void)    { return s_count > 0; }
