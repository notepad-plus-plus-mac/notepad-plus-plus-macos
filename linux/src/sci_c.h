/*
 * C-safe Scintilla interface.
 *
 * Scintilla.h in this repo has C++-only additions (<vector>, namespaces).
 * This header provides everything a pure-C caller needs.
 */
#ifndef SCI_C_H
#define SCI_C_H

#include <stdint.h>
#include <gtk/gtk.h>

typedef uintptr_t uptr_t;
typedef intptr_t  sptr_t;
typedef intptr_t  Sci_Position;

/* Text range — used by SCI_GETTEXTRANGEFULL (Sci_TextRange is deprecated in this Scintilla version) */
typedef struct { Sci_Position cpMin; Sci_Position cpMax; } Sci_CharacterRangeFull;
typedef struct { Sci_CharacterRangeFull chrg; char *lpstrText; } Sci_TextRangeFull;

/* SCNotification layout — must match Scintilla's internal struct exactly */
typedef struct {
    void        *hwndFrom;
    uptr_t       idFrom;
    unsigned int code;
} Sci_NotifyHeader;

typedef struct SCNotification {
    Sci_NotifyHeader nmhdr;
    Sci_Position     position;
    int              ch;
    int              modifiers;
    int              modificationType;
    const char      *text;
    Sci_Position     length;
    Sci_Position     linesAdded;
    int              message;
    uptr_t           wParam;
    sptr_t           lParam;
    Sci_Position     line;
    int              foldLevelNow;
    int              foldLevelPrev;
    int              margin;
    int              listType;
    int              x;
    int              y;
    int              token;
    Sci_Position     annotationLinesAdded;
    int              updated;
    int              listCompletionMethod;
    int              characterSource;
} SCNotification;

#include "ScintillaWidget.h"

/* ------------------------------------------------------------------ */
/* SCI_ message constants (values from Scintilla.iface)               */
/* ------------------------------------------------------------------ */
#define SCI_GETLENGTH           2006
#define SCI_GETCURRENTPOS       2008
#define SCI_REDO                2011
#define SCI_SELECTALL           2013
#define SCI_SETSAVEPOINT        2014
#define SCI_SETCODEPAGE         2037
#define SCI_GETCOLUMN           2129
#define SCI_GETMODIFY           2159
#define SCI_LINEFROMPOSITION    2166
#define SCI_EMPTYUNDOBUFFER     2175
#define SCI_UNDO                2176
#define SCI_CUT                 2177
#define SCI_COPY                2178
#define SCI_PASTE               2179
#define SCI_GETSELECTIONSTART   2143
#define SCI_GETSELECTIONEND     2145
#define SCI_GETLINECOUNT        2154
#define SCI_LINELENGTH          2350
#define SCI_GETTEXTRANGEFULL    2039
#define SCI_SETTARGETRANGE      2686
#define SCI_POSITIONFROMLINE    2167
#define SCI_REPLACESEL          2170
#define SCI_GETLINEENDPOSITION  2136
#define SCI_REPLACETARGET       2194
#define SCI_SETTEXT             2181
#define SCI_GETTEXT             2182
#define SCI_SETMARGINWIDTHN     2243
#define SCI_SETEDGEMODE         2094
#define SCI_SETEDGECOLUMN       2080
#define SCI_SETEDGECOLOUR       2098
#define SC_EDGE_NONE            0
#define SC_EDGE_LINE            1
#define SCI_SETMARGINTYPE       2240
#define SCI_SETMARGINSENSITIVE  2246
#define SCI_SETVIEWWS           2020
#define SCI_GETVIEWWS           2021
#define SCI_SETVIEWEOL          2034
#define SCI_GETVIEWEOL          2033
#define SCI_SETTABWIDTH         2036
#define SCI_GETTABWIDTH         2121
#define SCI_SETUSETABS          2124
#define SCI_GETUSETABS          2125
#define SCI_STYLESETFORE        2051
#define SCI_STYLESETBACK        2052  /* was wrong (2040=SCI_MARKERDEFINE) */
#define SCI_STYLESETBOLD        2053
#define SCI_STYLESETITALIC      2054
#define SCI_STYLESETFONT        2056
#define SCI_STYLESETSIZE        2055
#define SCI_STYLESETUNDERLINE   2059
#define SCI_STYLECLEARALL       2050
#define SCI_SETSELBACK          2068
#define SCI_SETCARETFORE        2069
#define SCI_SETWHITESPACEFORE   2084
#define SCI_SETCARETLINEVISIBLE 2096
#define SCI_SETCARETLINEBACK    2098
#define SCI_MARKERSETFORE       2041
#define SCI_MARKERSETBACK       2042
#define SCI_SETFOLDMARGINCOLOUR  2290
#define SCI_SETFOLDMARGINHICOLOUR 2291
#define SCI_MARKERENABLEHIGHLIGHT 2293
#define SCI_LINEUP              2302
#define SCI_LINEEND             2314
#define SCI_HOME                2312
#define SCI_NEWLINE             2329
#define SCI_LOWERCASE           2340
#define SCI_UPPERCASE           2341
#define SCI_LINEDELETE          2338
#define SCI_LINEDUPLICATE       2404
#define SCI_MOVESELECTEDLINESUP   2652
#define SCI_MOVESELECTEDLINESDOWN 2653
#define SCI_GOTOPOS             2025
#define SCI_GOTOLINE            2024
#define SCI_SCROLLCARET         2169
#define SCI_SETOVERTYPE         2186
#define SCI_GETOVERTYPE         2187
#define SCI_CONVERTEOLS         2029
#define SCI_SETEOLMODE          2031
#define SCI_GETEOLMODE          2030
#define SCI_SETLEXERLANGUAGE    4006
#define SCI_SETILEXER           4033
#define SCI_SETPROPERTY         4004
#define SCI_SETKEYWORDS         4005
#define SCI_COLOURISE           4003

/* Line / indentation */
#define SCI_GETLINE             2153
#define SCI_SETLINEINDENTATION  2126
#define SCI_GETLINEINDENTATION  2127
#define SCI_GETLINEINDENTPOSITION 2128
#define SCI_SETCURRENTPOS       2141
#define SCI_SETSEL              2160

/* ------------------------------------------------------------------ */
/* SCN_ notification codes                                            */
/* ------------------------------------------------------------------ */
#define SCN_CHARADDED           2001
#define SCN_SAVEPOINTREACHED    2002
#define SCN_SAVEPOINTLEFT       2003
#define SCN_UPDATEUI            2007
#define SCN_MODIFIED            2008

/* SC_UPDATE_ flags (used in SCNotification.updated for SCN_UPDATEUI) */
#define SC_UPDATE_CONTENT       0x1
#define SC_UPDATE_SELECTION     0x2

/* Margin types */
#define SC_MARGIN_SYMBOL        0
#define SC_MARGIN_NUMBER        1

/* Multi-selection */
#define SCI_SETMULTIPLESELECTION         2563
#define SCI_GETMULTIPLESELECTION         2564
#define SCI_SETADDITIONALSELECTIONTYPING 2565
#define SCI_GETSELECTIONS                2570
#define SCI_MULTIPLESELECTADDNEXT        2688
#define SCI_MULTIPLESELECTADDEACH        2689
#define SCI_SETMULTIPASTE                2614
#define SC_MULTIPASTE_EACH               1
#define SCI_WORDSTARTPOSITION            2266
#define SCI_WORDENDPOSITION              2267

/* Column / rectangular selection */
#define SCI_SETSELECTIONMODE            2422
#define SCI_GETSELECTIONMODE            2423
#define SC_SEL_STREAM                   0
#define SC_SEL_RECTANGLE                1
#define SCI_GETLINESELSTARTPOSITION     2424
#define SCI_GETLINESELENDPOSITION       2425
#define SCI_SETVIRTUALSPACEOPTIONS      2596
#define SCVS_RECTANGULARSELECTION       1
#define SCVS_USERACCESSIBLE             2

/* Undo grouping */
#define SCI_BEGINUNDOACTION             2078
#define SCI_ENDUNDOACTION               2079

/* Text insertion */
#define SCI_INSERTTEXT                  2003

/* Word wrap */
#define SCI_SETWRAPMODE         2268
#define SCI_GETWRAPMODE         2269
#define SC_WRAP_NONE            0
#define SC_WRAP_WORD            1

/* Whitespace visibility */
#define SC_WS_INVISIBLE         0
#define SC_WS_VISIBLEALWAYS     1

/* Encoding / EOL */
#define SC_CP_UTF8              65001
#define SC_EOL_CRLF             0
#define SC_EOL_CR               1
#define SC_EOL_LF               2

/* Style indices */
#define STYLE_DEFAULT           32
#define STYLE_LINENUMBER        33
#define STYLE_BRACELIGHT        34
#define STYLE_BRACEBAD          35

/* Fold marker numbers (the full 7-level tree hierarchy) */
#define SC_MARKNUM_FOLDEREND        25
#define SC_MARKNUM_FOLDEROPENMID    26
#define SC_MARKNUM_FOLDERMIDTAIL    27
#define SC_MARKNUM_FOLDERTAIL       28
#define SC_MARKNUM_FOLDERSUB        29
#define SC_MARKNUM_FOLDER           30
#define SC_MARKNUM_FOLDEROPEN       31

/* Fold mark shapes */
#define SC_MARK_VLINE               20
#define SC_MARK_LCORNER             21
#define SC_MARK_TCORNER             22
#define SC_MARK_BOXPLUS             23
#define SC_MARK_BOXPLUSCONNECTED    24
#define SC_MARK_BOXMINUS            25
#define SC_MARK_BOXMINUSCONNECTED   26

/* Fold messages and constants */
#define SCI_GETFOLDLEVEL        2223
#define SCI_FOLDLINE            2237
#define SCI_FOLDCHILDREN        2238
#define SCI_TOGGLEFOLD          2231
#define SCI_FOLDALL             2662
#define SCI_SETFOLDFLAGS        2233
#define SC_FOLDACTION_CONTRACT  0
#define SC_FOLDACTION_EXPAND    1
#define SC_FOLDLEVELBASE        0x400
#define SC_FOLDLEVELHEADERFLAG  0x2000
#define SC_FOLDLEVELNUMBERMASK  0x0FFF
#define SC_FOLDFLAG_LINEAFTER_CONTRACTED 0x010

/* Line number margin dynamic width */
#define SCI_TEXTWIDTH           2276

/* Indicators (mark styles) */
#define SCI_INDICSETSTYLE       2080
#define SCI_INDICSETFORE        2082
#define SCI_INDICSETALPHA       2523
#define SCI_SETINDICATORCURRENT 2500
#define SCI_INDICATORFILLRANGE  2504
#define SCI_INDICATORCLEARRANGE 2505
#define SCI_INDICATORSTART      2508
#define SCI_INDICATOREND        2509
#define INDIC_ROUNDBOX          7
#define INDIC_STRAIGHTBOX       8
#define INDIC_FULLBOX           16

/* Bookmark marker */
#define SC_MARKNUM_BOOKMARK     1
#define SC_MARK_BOOKMARK        27
#define SC_MARK_ROUNDRECT       1

/* Caret / scroll preferences */
#define SCI_SETCARETPERIOD      2076
#define SCI_SETCARETWIDTH       2188
#define SCI_SETENDATLASTLINE    2277
#define SCI_LINECUT             2337
#define SCI_LINECOPY            2455

/* Brace matching / highlighting */
#define SCI_GETCHARAT           2007
#define SCI_BRACEMATCH          2353
#define SCI_BRACEHIGHLIGHT      2351
#define SCI_BRACEBADLIGHT       2352

/* Marker messages */
#define SCI_MARKERDEFINE        2040
#define SCI_MARKERADD           2043
#define SCI_MARKERDELETE        2044
#define SCI_MARKERDELETEALL     2045
#define SCI_MARKERGET           2046
#define SCI_MARKERNEXT          2047
#define SCI_MARKERPREV          2048

/* SCN_MARGINCLICK notification */
#define SCN_MARGINCLICK         2006

#endif /* SCI_C_H */
