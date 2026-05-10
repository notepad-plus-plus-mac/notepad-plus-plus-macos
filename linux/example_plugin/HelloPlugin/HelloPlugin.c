/*
 * HelloPlugin — minimal example plugin for Notetux++
 *
 * Build:
 *   gcc -shared -fPIC -o HelloPlugin.so HelloPlugin.c
 *
 * Install:
 *   mkdir -p ~/.config/notetux/plugins/HelloPlugin
 *   cp HelloPlugin.so ~/.config/notetux/plugins/HelloPlugin/
 */

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>

/* Re-declare the FuncItem type (copy from plugin.h or include it) */
typedef struct {
    char   itemName[64];
    void (*pFunc)(void);
    int    cmdID;
    int    init2Check;
} FuncItem;

typedef long (*NppHostMsg)(unsigned int, unsigned long, long);

typedef struct {
    void       *nppHandle;
    void       *scintillaMainHandle;
    void       *scintillaSecondHandle;
    NppHostMsg  hostMsg;
} NppData;

#define NPPM_BASE              (0x0400 + 1000)
#define NPPM_GETCURRENTSCINTILLA (NPPM_BASE + 4)
#define NPPM_GETFULLCURRENTPATH  (NPPM_BASE + 38)

/* Plugin state */
static NppHostMsg s_host_msg = NULL;

/* ---- Plugin functions ---- */

static void hello_action(void)
{
    char path[2048] = "(no file)";
    if (s_host_msg)
        s_host_msg(NPPM_GETFULLCURRENTPATH, 0, (long)(intptr_t)path);

    GtkWidget *dlg = gtk_message_dialog_new(
        NULL, GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "Hello from HelloPlugin!\n\nCurrent file:\n%s", path);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void about_action(void)
{
    GtkWidget *dlg = gtk_message_dialog_new(
        NULL, GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "HelloPlugin v1.0\nA minimal example plugin for Notetux++.");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* ---- NPP plugin API ---- */

static FuncItem s_funcs[] = {
    { "Say Hello",    hello_action, 0, 0 },
    { "-",            NULL,         0, 0 },  /* separator */
    { "About…",       about_action, 0, 0 },
};
#define N_FUNCS (int)(sizeof(s_funcs) / sizeof(s_funcs[0]))

const char *getName(void)
{
    return "Hello Plugin";
}

FuncItem *getFuncsArray(int *nbF)
{
    *nbF = N_FUNCS;
    return s_funcs;
}

void beNotified(void *pNotify)
{
    (void)pNotify; /* not used by this plugin */
}

long messageProc(unsigned int msg, unsigned long wParam, long lParam)
{
    (void)msg; (void)wParam; (void)lParam;
    return 0L;
}

int isUnicode(void)
{
    return 1;
}

void setInfo(NppData data)
{
    s_host_msg = data.hostMsg;
}
