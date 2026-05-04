/* lexer.c — Lexilla integration for the Linux GTK3 port.
 * Ports setLanguage: / applyKeywords: / extensionLanguageMap from EditorView.mm.
 */
#include "lexer.h"
#include "sci_c.h"
#include "stylestore.h"

#include <string.h>
#include <ctype.h>

/* Bridge to the C++ CreateLexer() API */
extern void *lexilla_create_lexer(const char *name);

/* ------------------------------------------------------------------ */
/* Tables (ported directly from EditorView.mm)                        */
/* ------------------------------------------------------------------ */

typedef struct { const char *ext; const char *lang; } ExtLang;
static const ExtLang kExtLang[] = {
    /* C-family */
    {"c",   "c"},     {"h",   "c"},
    {"cpp", "cpp"},   {"cxx", "cpp"},  {"cc",  "cpp"},
    {"hpp", "cpp"},   {"hxx", "cpp"},
    {"m",   "objc"},  {"mm",  "objc"},
    {"cs",  "cs"},
    {"java","java"},
    {"js",  "javascript"}, {"mjs","javascript"}, {"jsx","javascript"},
    {"ts",  "typescript"}, {"tsx","typescript"},
    {"swift","swift"},
    {"rc",  "rc"},
    {"as",  "actionscript"},
    /* Web */
    {"html","html"},  {"htm", "html"},
    {"asp", "asp"},   {"aspx","asp"},
    {"xml", "xml"},   {"xsl", "xml"},  {"xslt","xml"},
    {"svg", "xml"},   {"plist","xml"},
    {"css", "css"},   {"scss","css"},  {"less","css"},
    {"json","json"},
    {"php", "php"},
    /* Scripting */
    {"py",  "python"}, {"pyw","python"},
    {"rb",  "ruby"},   {"rake","ruby"}, {"gemspec","ruby"},
    {"pl",  "perl"},   {"pm", "perl"},
    {"lua", "lua"},
    {"sh",  "bash"},   {"bash","bash"}, {"zsh","bash"},
    {"ps1", "powershell"}, {"psm1","powershell"},
    {"bat", "batch"},  {"cmd","batch"},
    {"tcl", "tcl"},
    {"r",   "r"},      {"R",  "r"},
    {"coffee","coffeescript"},
    /* Systems */
    {"rs",  "rust"},
    {"go",  "go"},
    {"d",   "d"},
    /* Markup / Config */
    {"md",  "markdown"}, {"markdown","markdown"},
    {"tex", "latex"},  {"latex","latex"},
    {"yml", "yaml"},   {"yaml","yaml"},
    {"toml","toml"},
    {"ini", "ini"},    {"cfg","ini"},   {"conf","ini"},
    {"properties","props"},
    {"makefile","makefile"}, {"mk","makefile"},
    {"cmake","cmake"},
    {"diff","diff"},   {"patch","diff"},
    {"reg", "registry"},
    {"nsi", "nsis"},   {"nsh","nsis"},
    {"iss", "inno"},
    /* Database */
    {"sql", "sql"},
    /* Scientific */
    {"f",   "fortran"}, {"f90","fortran"}, {"f95","fortran"},
    {"f77", "fortran77"},
    {"pas", "pascal"},  {"pp","pascal"},
    {"hs",  "haskell"}, {"lhs","haskell"},
    {"ml",  "caml"},    {"mli","caml"},
    {"erl", "erlang"},
    {"nim", "nim"},
    {"gd",  "gdscript"},
    {"sas", "sas"},
    /* Hardware */
    {"vhd", "vhdl"},   {"vhdl","vhdl"},
    {"v",   "verilog"},{"sv", "verilog"},
    {"asm", "asm"},    {"s",  "asm"},
    /* Other */
    {"ada", "ada"},    {"adb","ada"},   {"ads","ada"},
    {"cob", "cobol"},  {"cbl","cobol"},
    {"vb",  "vb"},     {"vbs","vb"},    {"bas","vb"},
    {"au3", "autoit"},
    {"ps",  "postscript"}, {"eps","postscript"},
    {"mat", "matlab"},
    /* Smalltalk */
    {"st",  "smalltalk"},
    /* Forth */
    {"forth","forth"},
    /* OScript */
    {"osx", "oscript"},
    /* AVS */
    {"avs", "avs"},   {"avsi","avs"},
    /* Hollywood */
    {"hws", "hollywood"},
    /* PureBasic */
    {"pb",  "purebasic"},
    /* FreeBasic */
    {"bi",  "freebasic"},
    /* BlitzBasic */
    {"bb",  "blitzbasic"},
    /* KiX */
    {"kix", "kix"},
    /* VisualProlog */
    {"pro", "visualprolog"},
    /* BaanC */
    {"bc",  "baanc"},  {"cln","baanc"},
    /* NNCronTab */
    {"tab", "nncrontab"}, {"spf","nncrontab"},
    /* CSound */
    {"orc", "csound"}, {"sco","csound"}, {"csd","csound"},
    /* EScript */
    {"src", "escript"}, {"em", "escript"},
    /* Spice */
    {"scp", "spice"},
    {NULL, NULL}
};

typedef struct { const char *lang; const char *lexer; } LangLexer;
static const LangLexer kLangLexer[] = {
    /* C-family */
    {"c",           "cpp"},
    {"cpp",         "cpp"},
    {"objc",        "cpp"},
    {"cs",          "cpp"},
    {"java",        "cpp"},
    {"javascript",  "cpp"},
    {"typescript",  "cpp"},
    {"swift",       "cpp"},
    {"rc",          "cpp"},
    {"actionscript","cpp"},
    {"go",          "cpp"},
    /* Web */
    {"html",        "hypertext"},
    {"asp",         "hypertext"},
    {"xml",         "xml"},
    {"css",         "css"},
    {"json",        "json"},
    {"php",         "phpscript"},
    /* Scripting */
    {"python",      "python"},
    {"ruby",        "ruby"},
    {"perl",        "perl"},
    {"lua",         "lua"},
    {"bash",        "bash"},
    {"powershell",  "powershell"},
    {"batch",       "batch"},
    {"tcl",         "tcl"},
    {"r",           "r"},
    {"raku",        "raku"},
    {"coffeescript","coffeescript"},
    /* Systems */
    {"rust",        "rust"},
    {"d",           "d"},
    /* Markup / Config */
    {"markdown",    "markdown"},
    {"latex",       "latex"},
    {"tex",         "tex"},
    {"yaml",        "yaml"},
    {"toml",        "toml"},
    {"ini",         "props"},
    {"props",       "props"},
    {"makefile",    "makefile"},
    {"cmake",       "cmake"},
    {"diff",        "diff"},
    {"registry",    "registry"},
    {"nsis",        "nsis"},
    {"inno",        "inno"},
    /* Database */
    {"sql",         "sql"},
    {"mssql",       "mssql"},
    /* Scientific */
    {"fortran",     "fortran"},
    {"fortran77",   "f77"},
    {"pascal",      "pascal"},
    {"haskell",     "haskell"},
    {"caml",        "caml"},
    {"lisp",        "lisp"},
    {"scheme",      "lisp"},
    {"erlang",      "erlang"},
    {"nim",         "nim"},
    {"gdscript",    "gdscript"},
    {"sas",         "sas"},
    /* Hardware */
    {"vhdl",        "vhdl"},
    {"verilog",     "verilog"},
    {"asm",         "asm"},
    /* Other */
    {"ada",         "ada"},
    {"cobol",       "COBOL"},
    {"vb",          "vb"},
    {"autoit",      "au3"},
    {"postscript",  "ps"},
    {"matlab",      "matlab"},
    {"smalltalk",   "smalltalk"},
    {"forth",       "forth"},
    {"oscript",     "oscript"},
    {"avs",         "avs"},
    {"hollywood",   "hollywood"},
    {"purebasic",   "purebasic"},
    {"freebasic",   "freebasic"},
    {"blitzbasic",  "blitzbasic"},
    {"kix",         "kix"},
    {"visualprolog","visualprolog"},
    {"baanc",       "baan"},
    {"nncrontab",   "nncrontab"},
    {"csound",      "csound"},
    {"escript",     "escript"},
    {"spice",       "spice"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static sptr_t sci_msg(GtkWidget *sci, unsigned int m, uptr_t w, sptr_t l)
{
    return scintilla_send_message(SCINTILLA(sci), m, w, l);
}

static const char *ext_to_lang(const char *ext)
{
    if (!ext || !*ext) return NULL;
    char low[32];
    int i;
    for (i = 0; ext[i] && i < 31; i++)
        low[i] = (char)tolower((unsigned char)ext[i]);
    low[i] = '\0';

    for (const ExtLang *e = kExtLang; e->ext; e++)
        if (strcmp(e->ext, low) == 0)
            return e->lang;
    return NULL;
}

static const char *lang_to_lexer(const char *lang)
{
    if (!lang || !*lang) return NULL;
    char low[64];
    int i;
    for (i = 0; lang[i] && i < 63; i++)
        low[i] = (char)tolower((unsigned char)lang[i]);
    low[i] = '\0';

    for (const LangLexer *l = kLangLexer; l->lang; l++)
        if (strcmp(l->lang, low) == 0)
            return l->lexer;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Keyword fallbacks (ported from EditorView.mm applyKeywords:)       */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Keyword table (shared with autocomplete)                           */
/* ------------------------------------------------------------------ */

typedef struct { const char *lang; const char *keywords; } LangKeywords;

static const LangKeywords kKeywords[] = {
    { "cpp",
      "alignas alignof and and_eq asm auto bitand bitor bool break case catch char "
      "char8_t char16_t char32_t class compl concept const consteval constexpr constinit "
      "const_cast continue co_await co_return co_yield decltype default delete do double "
      "dynamic_cast else enum explicit export extern false float for friend goto if inline "
      "int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private "
      "protected public register reinterpret_cast requires return short signed sizeof static "
      "static_assert static_cast struct switch template this thread_local throw true try "
      "typedef typeid typename union unsigned using virtual void volatile wchar_t while "
      "xor xor_eq" },
    { "python",
      "False None True and as assert async await break class continue def del "
      "elif else except finally for from global if import in is lambda nonlocal not or "
      "pass raise return try while with yield" },
    { "javascript",
      "async await break case catch class const continue debugger default "
      "delete do else export extends false finally for from function if import in "
      "instanceof let new null of return static super switch this throw true try typeof "
      "undefined var void while with yield" },
    { "sql",
      "add all alter and any as asc authorization backup begin between by "
      "cascade case check close clustered coalesce column commit compute constraint "
      "contains containstable continue convert create cross current current_date "
      "current_time cursor database dbcc deallocate declare default delete deny desc "
      "distinct distributed double drop dump else end errlvl escape except exec execute "
      "exists exit external fetch file fillfactor for foreign freetext freetexttable "
      "from full function goto grant group having holdlock identity identitycol "
      "identity_insert if in index inner insert intersect into is join key kill left "
      "like lineno load merge national nocheck nonclustered not null nullif of off "
      "offsets on open opendatasource openquery openrowset openxml option or order outer "
      "over percent pivot plan precision primary print proc procedure public raiserror "
      "read readtext reconfigure references replication restore restrict return revert "
      "revoke right rollback rowcount rowguidcol rule save schema securityaudit select "
      "session_user set setuser shutdown some statistics system_user table tablesample "
      "textsize then to top tran transaction trigger truncate try_convert tsequal "
      "union unique unpivot update updatetext use user values varying view waitfor when "
      "where while with within writetext" },
    { "rust",
      "as async await break const continue crate dyn else enum extern false fn for "
      "if impl in let loop match mod move mut pub ref return self Self static struct "
      "super trait true type union unsafe use where while" },
    { "bash",
      "case do done elif else esac fi for function if in select then time until while "
      "alias bg bind break builtin caller cd command compgen complete compopt continue "
      "declare dirs disown echo enable eval exec exit export false fc fg getopts hash "
      "help history jobs kill let local logout mapfile popd printf pushd pwd read "
      "readarray readonly return set shift shopt source suspend test times trap true "
      "type typeset ulimit umask unalias unset wait" },
    { "lua",
      "and break do else elseif end false for function goto if in local nil not or "
      "repeat return then true until while" },
    { "php",
      "abstract and array as break callable case catch class clone const continue "
      "declare default do echo else elseif empty enddeclare endfor endforeach endif "
      "endswitch endwhile enum extends false final finally fn for foreach function "
      "global goto if implements include include_once instanceof insteadof interface "
      "isset list match namespace new null or print private protected public readonly "
      "require require_once return static switch throw trait true try unset use var "
      "while xor yield" },
    { "ruby",
      "__FILE__ __LINE__ __ENCODING__ begin defined? do end false in module nil "
      "not or raise rescue retry return self super then true undef unless until when "
      "while yield alias and break case class def else elsif ensure for if" },
    { "perl",
      "abs accept alarm and atan2 bind binmode bless caller chdir chmod chomp chop "
      "chown chr chroot close closedir connect continue cos crypt dbmclose dbmopen "
      "defined delete die do dump each else elsif endgrent endhostent endnetent "
      "endprotoent endpwent endservent eof eval exec exists exit exp fcntl fileno "
      "flock for foreach fork format getc getgrent getgrgid getgrnam gethostbyaddr "
      "gethostbyname gethostent getlogin getnetbyaddr getnetbyname getnetent "
      "getpeername getpgrp getppid getpriority getprotobyname getprotobynumber "
      "getprotoent getpwent getpwnam getpwuid getservbyname getservbyport getservent "
      "getsockname getsockopt glob gmtime goto grep hex if import index int ioctl "
      "join keys kill last lc lcfirst length link listen local localtime log lstat "
      "map mkdir msgctl msgget msgrcv msgsnd my next no not oct open opendir or ord "
      "our pack package pipe pop pos print printf prototype push quotemeta rand read "
      "readdir readline readlink readpipe recv redo ref rename require reset return "
      "reverse rewinddir rindex rmdir say scalar seek seekdir select semctl semget "
      "semop send setgrent sethostent setnetent setpgrp setpriority setprotoent "
      "setpwent setservent setsockopt shift shmctl shmget shmread shmwrite shutdown "
      "sin sleep socket socketpair sort splice split sprintf sqrt srand stat study "
      "sub substr symlink syscall sysopen sysread sysseek system syswrite tell telldir "
      "tie tied time times truncate uc ucfirst umask undef unless unlink unpack unshift "
      "untie use utime values vec wait waitpid wantarray warn while write" },
    { NULL, NULL }
};

/* Returns keyword string for lang_name, applying aliases (c→cpp, typescript→javascript).
   Returns NULL if no keywords are defined for that language. */
const char *lexer_get_keywords(const char *lang_name)
{
    if (!lang_name || !*lang_name) return NULL;
    const char *kw_lang = lang_name;
    if (strcmp(kw_lang, "c") == 0 || strcmp(kw_lang, "objc") == 0) kw_lang = "cpp";
    if (strcmp(kw_lang, "typescript") == 0) kw_lang = "javascript";
    for (const LangKeywords *k = kKeywords; k->lang; k++)
        if (strcmp(k->lang, kw_lang) == 0) return k->keywords;
    return NULL;
}

static void apply_keywords(GtkWidget *sci, const char *lang)
{
    const char *kw = lexer_get_keywords(lang);
    if (kw)
        sci_msg(sci, SCI_SETKEYWORDS, 0, (sptr_t)kw);
}

/* ------------------------------------------------------------------ */
/* Folding properties (ported from EditorView.mm setLanguage:)        */
/* ------------------------------------------------------------------ */

static void apply_fold_props(GtkWidget *sci, const char *lang)
{
    sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold",         (sptr_t)"1");
    sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.compact", (sptr_t)"0");

    if (!lang) return;

    static const char *c_family[] = {
        "c","cpp","objc","cs","java","javascript","typescript",
        "swift","go","rust","d","actionscript","rc", NULL
    };
    for (int i = 0; c_family[i]; i++) {
        if (strcmp(lang, c_family[i]) == 0) {
            sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.comment",      (sptr_t)"1");
            sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.preprocessor", (sptr_t)"1");
            return;
        }
    }
    if (strcmp(lang,"html")==0 || strcmp(lang,"xml")==0 ||
        strcmp(lang,"asp")==0  || strcmp(lang,"php")==0) {
        sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.html",              (sptr_t)"1");
        sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.html.preprocessor",(sptr_t)"1");
        sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.hypertext.comment", (sptr_t)"1");
        sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.hypertext.heredoc", (sptr_t)"1");
    } else if (strcmp(lang,"python")==0) {
        sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.quotes.python",    (sptr_t)"1");
    } else if (strcmp(lang,"lua")==0) {
        sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.comment.lua",      (sptr_t)"1");
    } else if (strcmp(lang,"sql")==0 || strcmp(lang,"mssql")==0) {
        sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.comment",          (sptr_t)"1");
        sci_msg(sci, SCI_SETPROPERTY, (uptr_t)"fold.sql.only.begin",   (sptr_t)"1");
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void lexer_apply(GtkWidget *sci, const char *lang_name)
{
    g_object_set_data_full(G_OBJECT(sci), "npp-lang",
                           lang_name ? g_strdup(lang_name) : g_strdup(""),
                           g_free);

    stylestore_apply_default(sci);
    sci_msg(sci, SCI_STYLECLEARALL, 0, 0);
    stylestore_apply_global(sci);

    if (!lang_name || !*lang_name) {
        sci_msg(sci, SCI_SETILEXER, 0, 0);
        sptr_t docLen = sci_msg(sci, SCI_GETLENGTH, 0, 0);
        if (docLen > 0)
            sci_msg(sci, SCI_COLOURISE, 0, docLen);
        return;
    }

    const char *lexer_name = lang_to_lexer(lang_name);
    if (!lexer_name) {
        sci_msg(sci, SCI_SETILEXER, 0, 0);
        return;
    }

    void *lexer = lexilla_create_lexer(lexer_name);
    if (lexer)
        sci_msg(sci, SCI_SETILEXER, 0, (sptr_t)lexer);

    apply_fold_props(sci, lang_name);
    apply_keywords(sci, lang_name);
    stylestore_apply_lexer(sci, lang_name);

    sptr_t docLen = sci_msg(sci, SCI_GETLENGTH, 0, 0);
    if (docLen > 0)
        sci_msg(sci, SCI_COLOURISE, 0, docLen);
}

void lexer_apply_from_path(GtkWidget *sci, const char *path)
{
    if (!path || !*path) {
        lexer_apply(sci, NULL);
        return;
    }
    const char *slash = strrchr(path, '/');
    const char *base  = slash ? slash + 1 : path;
    const char *dot   = strrchr(base, '.');
    const char *ext   = (dot && dot > base) ? dot + 1 : "";

    const char *lang = ext_to_lang(ext);
    lexer_apply(sci, lang);
}

const char *lexer_display_name(const char *lang_name)
{
    if (!lang_name || !*lang_name) return "Normal Text";
    return lang_name;
}
