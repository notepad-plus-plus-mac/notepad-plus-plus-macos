#include "docmap.h"
#include "editor.h"
#include "sci_c.h"
#include "stylestore.h"
#include "lexer.h"
#include <gtk/gtk.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Module state                                                       */
/* ------------------------------------------------------------------ */

static GtkWidget *s_panel        = NULL;  /* the GtkOverlay            */
static GtkWidget *s_mini         = NULL;  /* minimap ScintillaWidget   */
static GtkWidget *s_overlay_area = NULL;  /* viewport highlight + input*/
static GtkWidget *s_main_sci     = NULL;  /* current main-editor sci   */

/* Cached scroll info, updated from docmap_sync_scroll() */
static int s_first_visible = 0;
static int s_visible_lines = 0;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void scroll_to_y(double y)
{
    if (!s_main_sci || !s_mini) return;

    int line_h = (int)scintilla_send_message(SCINTILLA(s_mini),
                                             SCI_TEXTHEIGHT, 0, 0);
    if (line_h <= 0) return;

    int mini_first   = (int)scintilla_send_message(SCINTILLA(s_mini),
                                                   SCI_GETFIRSTVISIBLELINE,
                                                   0, 0);
    int clicked_line = mini_first + (int)(y / line_h);

    /* Centre clicked line in the main editor */
    int new_first = clicked_line - s_visible_lines / 2;
    if (new_first < 0) new_first = 0;
    scintilla_send_message(SCINTILLA(s_main_sci),
                           SCI_SETFIRSTVISIBLELINE, (uptr_t)new_first, 0);
}

/* ------------------------------------------------------------------ */
/* Viewport overlay drawing                                           */
/* ------------------------------------------------------------------ */

static gboolean on_overlay_draw(GtkWidget *w, cairo_t *cr, gpointer d)
{
    (void)d;
    if (!s_mini || s_visible_lines <= 0) return FALSE;

    int line_h = (int)scintilla_send_message(SCINTILLA(s_mini),
                                             SCI_TEXTHEIGHT, 0, 0);
    if (line_h <= 0) return FALSE;

    int mini_first = (int)scintilla_send_message(SCINTILLA(s_mini),
                                                 SCI_GETFIRSTVISIBLELINE,
                                                 0, 0);
    int y  = (s_first_visible - mini_first) * line_h;
    int h  = s_visible_lines * line_h;
    int ww = gtk_widget_get_allocated_width(w);

    /* Fill */
    cairo_set_source_rgba(cr, 0.5, 0.72, 1.0, 0.22);
    cairo_rectangle(cr, 0, y, ww, h);
    cairo_fill(cr);

    /* Border */
    cairo_set_source_rgba(cr, 0.5, 0.72, 1.0, 0.6);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, y + 0.5, ww - 1, h - 1);
    cairo_stroke(cr);

    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Pointer events on the overlay → scroll main editor                 */
/* ------------------------------------------------------------------ */

static gboolean on_map_button_press(GtkWidget *w, GdkEventButton *ev,
                                    gpointer d)
{
    (void)w; (void)d;
    if (ev->button != 1) return FALSE;
    scroll_to_y(ev->y);
    return TRUE;
}

static gboolean on_map_motion(GtkWidget *w, GdkEventMotion *ev, gpointer d)
{
    (void)w; (void)d;
    if (!(ev->state & GDK_BUTTON1_MASK)) return FALSE;
    scroll_to_y(ev->y);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Apply minimap-specific view settings                               */
/* ------------------------------------------------------------------ */

static gboolean on_mini_key_press(GtkWidget *w, GdkEventKey *ev, gpointer d)
{
    (void)w; (void)ev; (void)d;
    return TRUE;  /* swallow all key events — minimap is display-only */
}

static void apply_minimap_settings(void)
{
    if (!s_mini) return;

    /* Do NOT call SCI_SETREADONLY here: after SCI_SETDOCPOINTER the minimap
     * shares the main editor's Document, so setting readonly would lock the
     * main editor too.  Keyboard input is blocked via on_mini_key_press. */
    scintilla_send_message(SCINTILLA(s_mini), SCI_SETZOOM,
                           (uptr_t)(sptr_t)(-10), 0);
    scintilla_send_message(SCINTILLA(s_mini), SCI_SETWRAPMODE,
                           SC_WRAP_NONE, 0);

    /* Hide all margins */
    for (int m = 0; m < 5; m++)
        scintilla_send_message(SCINTILLA(s_mini),
                               SCI_SETMARGINWIDTHN, (uptr_t)m, 0);

    /* No scrollbars */
    scintilla_send_message(SCINTILLA(s_mini), SCI_SETHSCROLLBAR, 0, 0);
    scintilla_send_message(SCINTILLA(s_mini), SCI_SETVSCROLLBAR, 0, 0);

    /* Invisible caret */
    scintilla_send_message(SCINTILLA(s_mini), SCI_SETCARETWIDTH, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

GtkWidget *docmap_init(void)
{
    /* Minimap Scintilla */
    s_mini = scintilla_new();
    gtk_widget_add_events(s_mini, GDK_KEY_PRESS_MASK);
    g_signal_connect(s_mini, "key-press-event",
                     G_CALLBACK(on_mini_key_press), NULL);
    apply_minimap_settings();

    /* Overlay drawing area: captures all pointer events so Scintilla
     * never handles clicks itself (no cursor placement, no selection). */
    s_overlay_area = gtk_drawing_area_new();
    gtk_widget_set_app_paintable(s_overlay_area, TRUE);
    gtk_widget_add_events(s_overlay_area,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK);
    g_signal_connect(s_overlay_area, "draw",
                     G_CALLBACK(on_overlay_draw), NULL);
    g_signal_connect(s_overlay_area, "button-press-event",
                     G_CALLBACK(on_map_button_press), NULL);
    g_signal_connect(s_overlay_area, "motion-notify-event",
                     G_CALLBACK(on_map_motion), NULL);

    /* GtkOverlay: Scintilla as base, drawing area on top */
    s_panel = gtk_overlay_new();
    gtk_widget_set_size_request(s_panel, 120, -1);
    gtk_container_add(GTK_CONTAINER(s_panel), s_mini);
    gtk_overlay_add_overlay(GTK_OVERLAY(s_panel), s_overlay_area);

    gtk_widget_hide(s_panel);
    return s_panel;
}

void docmap_update(GtkWidget *sci)
{
    if (!s_mini || !sci) return;
    s_main_sci = sci;

    /* Share the document: must drop read-only first */
    scintilla_send_message(SCINTILLA(s_mini), SCI_SETREADONLY, 0, 0);
    sptr_t doc = (sptr_t)scintilla_send_message(SCINTILLA(sci),
                                                SCI_GETDOCPOINTER, 0, 0);
    scintilla_send_message(SCINTILLA(s_mini), SCI_SETDOCPOINTER, 0, doc);

    /* Apply styles and lexer so syntax colours show in the minimap */
    stylestore_apply_default(s_mini);
    scintilla_send_message(SCINTILLA(s_mini), SCI_STYLECLEARALL, 0, 0);
    stylestore_apply_global(s_mini);
    const char *lang = (const char *)g_object_get_data(G_OBJECT(sci), "npp-lang");
    if (lang) {
        lexer_apply(s_mini, lang);
        stylestore_apply_lexer(s_mini, lang);
    }

    /* Re-apply minimap view settings (overrides any style-reset side effects) */
    apply_minimap_settings();

    docmap_sync_scroll(sci);
}

void docmap_sync_scroll(GtkWidget *sci)
{
    if (!s_mini || !sci || !docmap_is_visible()) return;
    s_main_sci = sci;

    s_first_visible = (int)scintilla_send_message(SCINTILLA(sci),
                                                  SCI_GETFIRSTVISIBLELINE,
                                                  0, 0);
    s_visible_lines = (int)scintilla_send_message(SCINTILLA(sci),
                                                  SCI_LINESONSCREEN, 0, 0);

    /* Position minimap so the visible range is centred in it */
    int mini_h    = gtk_widget_get_allocated_height(s_mini);
    int line_h    = (int)scintilla_send_message(SCINTILLA(s_mini),
                                               SCI_TEXTHEIGHT, 0, 0);
    int mini_vis  = (line_h > 0) ? (mini_h / line_h) : 60;
    int total     = (int)scintilla_send_message(SCINTILLA(sci),
                                               SCI_GETLINECOUNT, 0, 0);

    int center     = s_first_visible + s_visible_lines / 2;
    int mini_first = center - mini_vis / 2;
    if (mini_first < 0) mini_first = 0;
    if (mini_first > total - mini_vis) mini_first = total - mini_vis;
    if (mini_first < 0) mini_first = 0;

    scintilla_send_message(SCINTILLA(s_mini), SCI_SETFIRSTVISIBLELINE,
                           (uptr_t)mini_first, 0);

    gtk_widget_queue_draw(s_overlay_area);
}

void docmap_set_visible(gboolean v)
{
    if (!s_panel) return;
    if (v) gtk_widget_show(s_panel);
    else   gtk_widget_hide(s_panel);
}

gboolean docmap_is_visible(void)
{
    return s_panel && gtk_widget_get_visible(s_panel);
}
