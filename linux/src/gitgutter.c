#include "gitgutter.h"
#include "sci_c.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Scintilla colour constants (BGR)                                    */
/* ------------------------------------------------------------------ */
#define GG_COLOR_ADDED    0x009900  /* green  */
#define GG_COLOR_MODIFIED 0x0088FF  /* orange */
#define GG_COLOR_DELETED  0x0000CC  /* red    */

/* SC_MARK_FULLRECT and SC_MARK_LEFTRECT (not in sci_c.h) */
#define SC_MARK_FULLRECT  26
#define SC_MARK_LEFTRECT  27

/* ------------------------------------------------------------------ */
/* Per-sci state                                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    char     *path;          /* strdup'd file path */
    guint     timer_id;      /* debounce timer, 0 if none pending */
    GtkWidget *sci;          /* back-pointer (not ref-counted) */
} GutterState;

static sptr_t sci_msg(GtkWidget *sci, unsigned int m, uptr_t w, sptr_t l)
{
    return scintilla_send_message(SCINTILLA(sci), m, w, l);
}

static void free_state(gpointer p)
{
    GutterState *st = p;
    if (st->timer_id)
        g_source_remove(st->timer_id);
    g_free(st->path);
    g_free(st);
}

static GutterState *get_state(GtkWidget *sci)
{
    return g_object_get_data(G_OBJECT(sci), "npp-gutter");
}

/* ------------------------------------------------------------------ */
/* Setup                                                               */
/* ------------------------------------------------------------------ */

void gitgutter_setup(GtkWidget *sci)
{
    sci_msg(sci, SCI_SETMARGINTYPE,      GG_MARGIN, SC_MARGIN_SYMBOL);
    sci_msg(sci, SCI_SETMARGINSENSITIVE, GG_MARGIN, 0);
    sci_msg(sci, SCI_SETMARGINWIDTHN,    GG_MARGIN, 4);
    sci_msg(sci, SCI_SETMARGINMASKN,     GG_MARGIN, (sptr_t)GG_MASK);

    sci_msg(sci, SCI_MARKERDEFINE,  GG_MARK_ADDED,    SC_MARK_FULLRECT);
    sci_msg(sci, SCI_MARKERSETBACK, GG_MARK_ADDED,    GG_COLOR_ADDED);
    sci_msg(sci, SCI_MARKERSETFORE, GG_MARK_ADDED,    GG_COLOR_ADDED);

    sci_msg(sci, SCI_MARKERDEFINE,  GG_MARK_MODIFIED, SC_MARK_FULLRECT);
    sci_msg(sci, SCI_MARKERSETBACK, GG_MARK_MODIFIED, GG_COLOR_MODIFIED);
    sci_msg(sci, SCI_MARKERSETFORE, GG_MARK_MODIFIED, GG_COLOR_MODIFIED);

    sci_msg(sci, SCI_MARKERDEFINE,  GG_MARK_DELETED,  SC_MARK_LEFTRECT);
    sci_msg(sci, SCI_MARKERSETBACK, GG_MARK_DELETED,  GG_COLOR_DELETED);
    sci_msg(sci, SCI_MARKERSETFORE, GG_MARK_DELETED,  GG_COLOR_DELETED);

    GutterState *st = g_new0(GutterState, 1);
    st->sci = sci;
    g_object_set_data_full(G_OBJECT(sci), "npp-gutter", st, free_state);
}

/* ------------------------------------------------------------------ */
/* Unified diff parser                                                 */
/* ------------------------------------------------------------------ */

/*
 * line_type: 'A' added, 'M' modified, 'D' deleted, 0 unchanged
 * lines is indexed 0-based; caller must free.
 */
typedef struct {
    int   n_lines;
    char *type;   /* n_lines bytes: 'A' 'M' 'D' or 0 */
} DiffResult;

static DiffResult *parse_diff(const char *diff, int doc_lines)
{
    DiffResult *r = g_new0(DiffResult, 1);
    r->n_lines = doc_lines;
    r->type    = g_new0(char, doc_lines + 1);

    const char *p = diff;
    while (*p) {
        /* Find hunk header: @@ -old_start,old_count +new_start,new_count @@ */
        if (p[0] == '@' && p[1] == '@') {
            int old_start = 0, old_count = 1, new_start = 0, new_count = 1;
            /* skip "@@ " */
            const char *q = p + 2;
            while (*q == ' ') q++;
            if (*q == '-') {
                q++;
                old_start = atoi(q);
                while (*q && *q != ',' && *q != ' ') q++;
                if (*q == ',') { q++; old_count = atoi(q); }
            }
            while (*q && *q != '+') q++;
            if (*q == '+') {
                q++;
                new_start = atoi(q);
                while (*q && *q != ',' && *q != ' ') q++;
                if (*q == ',') { q++; new_count = atoi(q); }
            }
            /* skip to end of header line */
            while (*q && *q != '\n') q++;
            if (*q == '\n') q++;

            /* Scan hunk lines */
            int cur_new = new_start;   /* 1-based */
            int has_del  = 0;
            /* collect +lines and note whether any -lines exist */
            const char *hunk = q;
            /* first pass: detect if hunk has deletions */
            const char *scan = hunk;
            while (*scan && !(scan[0] == '@' && scan[1] == '@') &&
                   !(scan[0] == 'd' && scan[1] == 'i') &&
                   !(scan[0] == '\0')) {
                if (*scan == '-' && scan[1] != '-') { has_del = 1; break; }
                while (*scan && *scan != '\n') scan++;
                if (*scan == '\n') scan++;
            }

            /* second pass: annotate new-file lines */
            (void)old_count; (void)new_count; (void)old_start;
            while (*q && !(q[0] == '@' && q[1] == '@') &&
                   !(q[0] == 'd' && q[1] == 'i') ) {
                char ltype = *q;
                while (*q && *q != '\n') q++;
                if (*q == '\n') q++;

                if (ltype == '+') {
                    int idx = cur_new - 1;  /* 0-based */
                    if (idx >= 0 && idx < doc_lines) {
                        r->type[idx] = has_del ? 'M' : 'A';
                    }
                    cur_new++;
                } else if (ltype == ' ') {
                    cur_new++;
                } else if (ltype == '-') {
                    /* deleted: mark the line BEFORE the new_start as 'D',
                       only if there are no + lines (pure deletion hunk) */
                    (void)ltype;
                }
            }

            /* Pure deletion hunk: new_count == 0 → mark line before hunk */
            if (new_count == 0) {
                int idx = new_start - 1;   /* line before the deleted block */
                if (idx >= 0 && idx < doc_lines && r->type[idx] == 0)
                    r->type[idx] = 'D';
            }

            p = q;
            continue;
        }
        /* skip non-hunk lines */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Apply diff result to Scintilla markers                              */
/* ------------------------------------------------------------------ */

static void apply_diff(GtkWidget *sci, DiffResult *r)
{
    /* clear all gutter markers */
    sci_msg(sci, SCI_MARKERDELETEALL, GG_MARK_ADDED,    0);
    sci_msg(sci, SCI_MARKERDELETEALL, GG_MARK_MODIFIED, 0);
    sci_msg(sci, SCI_MARKERDELETEALL, GG_MARK_DELETED,  0);

    for (int i = 0; i < r->n_lines; i++) {
        int mark = -1;
        switch (r->type[i]) {
            case 'A': mark = GG_MARK_ADDED;    break;
            case 'M': mark = GG_MARK_MODIFIED; break;
            case 'D': mark = GG_MARK_DELETED;  break;
            default:  break;
        }
        if (mark >= 0)
            sci_msg(sci, SCI_MARKERADD, (uptr_t)i, (sptr_t)mark);
    }
}

/* ------------------------------------------------------------------ */
/* Async git subprocess                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    GtkWidget *sci;
    char      *path;
} DiffJob;

static void diff_stdout_ready(GObject *src, GAsyncResult *res, gpointer user_data)
{
    DiffJob *job = user_data;
    GInputStream *stream = G_INPUT_STREAM(src);
    GError *err = NULL;

    /* Read all output synchronously (we're already on the main thread via callback) */
    GBytes *bytes = g_input_stream_read_bytes_finish(stream, res, &err);
    if (err) { g_error_free(err); goto done; }
    if (!bytes) goto done;

    gsize sz;
    const char *data = g_bytes_get_data(bytes, &sz);

    /* Check that the widget is still alive */
    GutterState *st = get_state(job->sci);
    if (st && g_strcmp0(st->path, job->path) == 0) {
        int doc_lines = (int)sci_msg(job->sci, SCI_GETLINECOUNT, 0, 0);
        char *diff_str = g_strndup(data, sz);
        DiffResult *r = parse_diff(diff_str, doc_lines);
        apply_diff(job->sci, r);
        g_free(r->type);
        g_free(r);
        g_free(diff_str);
    }

    g_bytes_unref(bytes);
done:
    g_free(job->path);
    g_free(job);
}

static void run_git_diff(GtkWidget *sci, const char *path)
{
    gchar *dir = g_path_get_dirname(path);
    gchar *basename = g_path_get_basename(path);

    GError *err = NULL;
    const gchar *argv[] = { "git", "diff", "HEAD", "--", basename, NULL };
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_subprocess_launcher_set_cwd(launcher, dir);

    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, argv, &err);
    g_object_unref(launcher);
    g_free(dir);
    g_free(basename);

    if (!proc) {
        if (err) g_error_free(err);
        return;
    }

    GInputStream *stdout_stream = g_subprocess_get_stdout_pipe(proc);

    DiffJob *job = g_new0(DiffJob, 1);
    job->sci  = sci;
    job->path = g_strdup(path);

    /* Read up to 4 MB — enough for any reasonable diff */
    g_input_stream_read_bytes_async(stdout_stream, 4 * 1024 * 1024,
                                    G_PRIORITY_DEFAULT, NULL,
                                    diff_stdout_ready, job);
    g_object_unref(proc);
}

/* ------------------------------------------------------------------ */
/* Debounced update                                                    */
/* ------------------------------------------------------------------ */

static gboolean debounce_fire(gpointer user_data)
{
    GtkWidget *sci = user_data;
    GutterState *st = get_state(sci);
    if (!st || !st->path) return G_SOURCE_REMOVE;
    st->timer_id = 0;
    run_git_diff(sci, st->path);
    return G_SOURCE_REMOVE;
}

void gitgutter_update(GtkWidget *sci, const char *path)
{
    GutterState *st = get_state(sci);
    if (!st) return;

    if (!path || path[0] == '\0') {
        gitgutter_clear(sci);
        return;
    }

    g_free(st->path);
    st->path = g_strdup(path);

    if (st->timer_id) {
        g_source_remove(st->timer_id);
        st->timer_id = 0;
    }
    st->timer_id = g_timeout_add(800, debounce_fire, sci);
}

void gitgutter_clear(GtkWidget *sci)
{
    sci_msg(sci, SCI_MARKERDELETEALL, GG_MARK_ADDED,    0);
    sci_msg(sci, SCI_MARKERDELETEALL, GG_MARK_MODIFIED, 0);
    sci_msg(sci, SCI_MARKERDELETEALL, GG_MARK_DELETED,  0);

    GutterState *st = get_state(sci);
    if (st) {
        g_free(st->path);
        st->path = NULL;
        if (st->timer_id) {
            g_source_remove(st->timer_id);
            st->timer_id = 0;
        }
    }
}
