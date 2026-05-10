# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Project Is

A native macOS port of Notepad++ (the Windows text editor), built with C++17 and Objective-C++ (Cocoa). It wraps the vendored Scintilla editing engine and Lexilla syntax-highlighting library in a full Cocoa UI. Despite the repo name, this targets **macOS 11+** (Universal Binary: arm64 + x86_64), not Linux.

### Linux port (active development)

There is an **in-progress native GTK3 Linux port** living entirely in `linux/`. It is written in C11 with a thin C++ wrapper for Lexilla. It is a separate CMake project and does not share build infrastructure with the macOS app.

**Build the Linux port:**
```sh
cd linux
cmake -B build && cmake --build build
./build/notetux++
```

**Linux port source files (`linux/src/`):**

| File | Purpose |
|------|---------|
| `main.c` | GTK application entry point, menu bar, window setup |
| `editor.c/h` | Tab notebook, document open/save/close, `NppDoc` struct |
| `statusbar.c/h` | Bottom status bar (line, col, encoding, EOL, language) |
| `toolbar.c/h` | GTK3 toolbar with Fluent icon set |
| `findreplace.c/h` | Find/Replace dialog |
| `lexer.c/h` | Lexilla integration: extension→language→lexer maps, keyword lists, fold props |
| `lexilla_bridge.cpp` | Minimal C++ bridge: `lexilla_create_lexer()` wraps `CreateLexer()` for use from C |
| `i18n.c/h` | Locale detection, NPP XML parser, `T()`/`TM()` macros for translated strings |
| `stylestore.c/h` | Parses `stylers.model.xml` / user `~/.config/notetux/stylers.xml`; applies Scintilla styles |
| `styleeditor.c/h` | Style Configurator dialog (theme picker, per-language style editing) |
| `sci_c.h` | C-safe Scintilla constants and `SCNotification` layout |
| `encoding.c/h` | Encoding table (17 encodings), BOM detection, `g_convert` wrappers for open/save |
| `shortcutmap.c/h` | Shortcut table (27 entries), XML load/save, Shortcut Mapper dialog with key-capture |
| `prefs.c/h` | Preferences struct (`NppPrefs`), XML load/save, 4-page Preferences dialog |
| `findinfiles.c/h` | Find in Files dialog: directory walk, GThread search, collapsible GtkTreeView results |
| `columneditor.c/h` | Column Editor dialog: insert text or number sequence into each line of a selection |
| `autocomplete.c/h` | Word+keyword auto-completion: `SCI_AUTOCSHOW` driven by `SCN_CHARADDED`; sources keywords from `lexer_get_keywords()` and scans the document (first 100 KB) |
| `udl.c/h` | User Defined Language manager: scan and parse NPP UDL XML files; `udl_apply()` routes 28 kwlist slots to `SCI_SETPROPERTY` (comments, numbers, operators1, folders-in-code1, delimiters) or `SCI_SETKEYWORDS` (operators2, folders-in-code2/comment, keywords1-8); applies per-style colors/fonts; multi-word tokens preprocessed (`"a b"` → `a\vb`, `'a b'` → `a\bb`) |
| `gitgutter.c/h` | Git change-history gutter: `gitgutter_setup()` defines margin 3 + markers 2/3/4; `gitgutter_update()` debounces 800 ms then runs `git diff HEAD -- <file>` via `GSubprocess`; unified diff parser classifies lines as added/modified/deleted; `gitgutter_clear()` removes all markers |
| `macro.c/h` | Macro recording/playback: `macro_start_recording()`/`macro_stop_recording()` wraps `SCI_STARTRECORD`/`SCI_STOPRECORD`; `macro_on_record()` stores steps from `SCN_MACRORECORD` (string lParams heap-copied); `macro_playback()` replays once; `macro_playback_n()` prompts for count; each playback in one undo group |
| `session.c/h` | Session save/restore: `session_save()` serialises open file paths + `firstVisibleLine` + `xOffset` + `caretPosition` + `encoding` to `~/.config/notetux/session.xml` on quit; `session_restore()` reads it back with `GMarkupParser`, skips missing files, restores scroll/caret via `SCI_SETFIRSTVISIBLELINE` / `SCI_SETXOFFSET` / `SCI_GOTOPOS`; restore is skipped when CLI file arguments are present |
| `backup.c/h` | Auto-backup: `backup_init()` creates `~/.config/notetux/backup/` and starts a `g_timeout_add_seconds()` timer; `backup_tick()` iterates all open docs and writes modified ones to `~/.config/notetux/backup/<basename>`; `backup_clean(doc)` removes the backup on clean save (`SCN_SAVEPOINTREACHED`) and on tab close; interval and enable/disable controlled by `g_prefs.backup_interval_secs` / `g_prefs.backup_enabled`; `backup_restart_timer()` called when prefs change |
| `doclist.c/h` | Document List panel: `doclist_init()` creates a `GtkBox` with header + close button + `GtkListBox`; `doclist_refresh()` rebuilds rows from `editor_doc_at()` showing modified indicator and basename; `doclist_sync_selection(page)` highlights the active row; `doclist_set_visible()`/`doclist_is_visible()` control panel show/hide; panel lives in a `GtkPaned` left of the notebook; toggled from View → Panels → Document List; `main_doclist_refresh()` in `main.c` called from `editor.c` on new/open/close/save-as |
| `workspace.c/h` | Folder as Workspace panel: `workspace_init(window)` creates header (title + `…` open-folder button + `×` close) + path label + `GtkTreeView` backed by a `GtkTreeStore`; `workspace_set_folder(path)` clears and repopulates the tree with the root node expanded; lazy loading via dummy placeholder children — `on_row_expanded` removes the dummy and calls `populate_dir()` which uses `g_file_enumerate_children` (sync, dirs-first sorted); `render_icon` cell-data func sets `"folder"` or `"text-x-generic"` icon names; double-click on a file row calls `editor_open_path()`; hidden by default, toggled from View → Panels → Folder as Workspace or File → Open Folder as Workspace… (which also sets the folder and shows the panel) |
| `funclist.c/h` | Function List panel: `funclist_init()` creates a `GtkBox` with header + `×` close + `GtkTreeView` backed by `GtkTreeStore` (columns: `COL_LINE` int, `COL_NAME` str); `funclist_update(sci)` parses immediately; `funclist_schedule_update(sci)` debounces 600 ms then calls `do_parse()`; `do_parse()` fetches full text via `SCI_GETTEXT`, iterates lines, matches per-language `GRegex` patterns (18 languages), builds 2-level tree (class/struct nodes at root, functions as children, ungrouped under `(Global)`); Python uses indentation depth for class membership; all others use brace-depth tracking via `count_braces()`; clicking a row calls `SCI_GOTOLINE` + `SCI_SCROLLCARET`; patterns compiled once via `ensure_compiled()`; group header rows rendered bold via `render_name()` cell-data func; hidden by default, toggled from View → Panels → Function List; layout: workspace | doclist | notebook | funclist | docmap (nested `GtkPaned`) |
| `docmap.c/h` | Document Map panel: `docmap_init()` creates a `GtkBox` with header + `×` close + `GtkOverlay(ScintillaWidget, GtkDrawingArea)`; the minimap Scintilla shares the document with the main editor via `SCI_SETDOCPOINTER` (text + style tokens auto-mirrored); styles and lexer applied via `stylestore_apply_*` + `lexer_apply`; view settings: zoom −10, all margins hidden, no scrollbars, zero-width caret, word-wrap off; `docmap_sync_scroll(sci)` called from `SCN_UPDATEUI` (current tab only) — reads `SCI_GETFIRSTVISIBLELINE` + `SCI_LINESONSCREEN`, centres the minimap on the visible range, queues a redraw; `on_overlay_draw` paints a semi-transparent blue rectangle indicating the viewport; the overlay `GtkDrawingArea` captures all pointer events (blocks Scintilla's own mouse handling); click and drag both call `scroll_to_y()` → `SCI_SETFIRSTVISIBLELINE` on main editor; panel has no header (GtkOverlay is the root widget); hidden by default, toggled from View → Panels → Document Map |
| `searchresults.c/h` | Search Results panel: dockable bottom pane; `searchresults_init()` creates a `GtkBox` with header (title + match count label + Clear button + × close) + `GtkTreeView` backed by `GtkTreeStore`; 3-level tree: search root ("Search "needle" — N matches in M files", bold) → file nodes ("path (N hits)", semibold) → hit rows ("  line:\ttext", normal); accumulates across multiple searches without clearing; `searchresults_begin/add_file/add_hit/end()` called from `findinfiles.c:post_results()`; `end()` expands the new search root, scrolls to it, and auto-shows the panel; double-click a hit row calls `editor_open_and_goto()`; Clear button wipes all results; panel lives at the bottom in a vertical `GtkPaned` (pack2) below the horizontal panels (pack1); toggled from View → Panels → Search Results |
| `plugin.c/h` | Plugin system: `plugin_init(window)` sets up `NppData` (main window + host callback); `plugin_load_all()` scans `~/.config/notetux/plugins/<Name>/<Name>.so` and `/usr/lib/notetux/plugins/`; each `.so` must export `getName`, `getFuncsArray`, `beNotified`, `messageProc`, `isUnicode`; optional `setInfo(NppData)` receives host handle + `hostMsg` function pointer before `getFuncsArray`; `plugin_populate_menu(menu)` builds one submenu per plugin from `FuncItem` array (separator when `itemName=="-"`, `GtkCheckMenuItem` when `init2Check!=0`); `plugin_notify_all(SCNotification*)` broadcasts editor events to all loaded plugins (called from `editor.c:on_sci_notify`); `plugin_host_message()` routes `NPPM_GETCURRENTSCINTILLA`, `NPPM_GETNBOPENFILES`, `NPPM_GETFULLCURRENTPATH`, `NPPM_GETFILENAME`, `NPPM_GETDIRECTORYPATH`; command IDs assigned sequentially from 10000; plugin directory auto-created on first launch |
| `spell.c/h` | Spell checker: `spell_init(window)` loads `libenchant-2.so.2` via `dlopen` at runtime (no build-time dependency); opens a dictionary matching the system locale (`LC_MESSAGES`), falling back to the base language tag; `spell_on_sci_created(sci)` configures Scintilla indicator 8 as `INDIC_SQUIGGLE` red; `spell_schedule_check(sci)` debounces 1200 ms then calls `do_check()` which walks the first 200 KB as UTF-8, skipping words < 3 chars or all-uppercase, and marks misspellings with indicator 8; `spell_check_document(sci)` cancels any pending timer and runs immediately; enabled/disabled via Settings → Spell Check check menu item; right-click on a misspelled word calls `spell_populate_context_menu()` which prepends: header label, up to 8 suggestions (each replaces the word on click), "Ignore Word" (`enchant_dict_add_to_session`), "Add to Dictionary" (`enchant_dict_add`); context menu is built in `on_sci_button_press` (connected to each Scintilla widget in `setup_sci`); gracefully disabled if enchant library or dictionary unavailable at runtime |

**User config location (Linux port):** `~/.config/notetux/`
- `stylers.xml` — user style overrides (saved by Style Configurator)
- `themes/` — user theme XML files (scanned alongside bundled `resources/themes/`)
- `recentfiles.txt` — recently opened/saved files (one path per line, max 10)
- `shortcuts.xml` — user keyboard shortcut overrides
- `config.xml` — preferences (tab width, indent, caret, EOL, encoding, display options)
- `session.xml` — session state written on quit; restored on next launch when no CLI args given
- `backup/` — auto-backup copies of unsaved/modified documents (removed on save or close)
- `userDefineLangs/` — user UDL XML files (NPP format), merged with bundled ones

**Key design rules for the Linux port:**
- All UI code is C11; only `lexilla_bridge.cpp` is C++ (LexUserStub.cxx removed — real LexUser.cxx now compiled in lexilla)
- Scintilla color format is BGR: `r | (g<<8) | (b<<16)`
- Styling call order: `stylestore_apply_default()` → `SCI_STYLECLEARALL` → `stylestore_apply_global()` → install lexer → `stylestore_apply_lexer(sci, lang_name)` (pass `lang_name`, NOT the Lexilla lexer name)
- `stylestore_apply_lexer` must receive the XML `LexerType name` (e.g. `"php"`), not the Lexilla internal name (e.g. `"phpscript"`)
- System monospace font is auto-detected via GSettings on first run, replacing "Courier New"
- **GTK3 dialog pattern**: never use `gtk_dialog_run()` in a while-loop and never call `gtk_widget_destroy()` from within the dialog's own signal handler — both are unreliable on GTK3/Wayland. Use the `response` signal + `gtk_widget_hide()` with a persistent singleton (see `styleeditor.c`, `findreplace.c`).
- **XML parsing**: NPP theme/styler XML files may contain unescaped `&` in attribute values (e.g. `name="BUILTIN FUNC & TYPE"`). Always pre-process with `fix_bare_ampersands()` before passing to `GMarkupParser` (implemented in `stylestore.c`).
- **i18n response IDs**: when assigning `gtk_dialog_new_with_buttons` response IDs, verify the translated label matches the intended action — NPP localisation keys like `dlg.StyleConfig.2301` map to "Salva e chiudi" (Save and Close) in Italian, not "Apply to Editors".

**Release packaging (do after the project is feature-complete):**
Once all features are shipped, produce pre-compiled packages for all major distros: `.deb` (Debian/Ubuntu/Mint), `.rpm` (Fedora/RHEL/openSUSE), `.pkg.tar.zst` (Arch/Manjaro), `.apk` (Alpine). **Never AppImage, Flatpak, or Snap** — the developer is firmly against these formats; native distro packages only.

**Extra features (beyond original Notepad++ scope — implement only after all upcoming features are complete):**
- **Vim mode** — modal editing (Normal / Insert / Visual) with core Vim motions and commands; toggled via Settings → Vim Mode; implemented via `SCN_CHARADDED` / `key-press-event` interception.
- **Terminal panel** — embedded terminal emulator in the bottom panel; opens in the `cwd` of the current file or workspace folder when local; when the nppFTP plugin is available and the file is remote, opens an SSH terminal on the connected server.

**Known bugs fixed:**
- `stylestore`: `GMarkupParser` rejected `~/.config/notetux/stylers.xml` at lines containing `&` in attribute values (e.g. CaML `BUILTIN FUNC & TYPE`). Fixed in `stylestore.c:fix_bare_ampersands()` by escaping bare `&` before parsing.
- `styleeditor`: Style Configurator Save/Close buttons had no effect. Three root causes: (1) `gtk_dialog_run()` in a while-loop does not re-acquire the input grab on subsequent iterations under Wayland; (2) `gtk_widget_destroy()` called from within the `response` signal handler does not reliably close the window; (3) response IDs for "Salva" and "Salva e chiudi" were swapped. Fixed by converting to a persistent singleton dialog (hidden/shown like Find/Replace), using `gtk_widget_hide()` to close, and correcting the response-ID-to-action mapping.

## Build

Requires CMake 3.20+ and Xcode toolchain.

```sh
cmake -B build && cmake --build build
```

Output: `build/Notepad++.app` — a self-contained app bundle. Post-build steps copy XML resources, bundle localizations, and ad-hoc-sign the binary. No install step is needed; open the `.app` directly.

## Tests

No automated tests exist for the main application. Two test harnesses are available:

**Plugin load test** — verifies `.dylib` plugins export the required symbols:
```sh
cmake -S test_plugins -B test_plugins/build && cmake --build test_plugins/build
./test_plugins/build/test_plugins [optional_plugins_dir]
```

**Lexilla unit tests** (C++, makefile-based):
```sh
cd lexilla/test/unit && make
```

**Scintilla tests** (Python, in `scintilla/test/`):
```sh
cd scintilla/test && python3 simpleTests.py
```

## Architecture

### Layer overview

```
User code
  └── Scintilla (vendored, cocoa-specific branch in scintilla/cocoa/)
        └── Lexilla (vendored, ~80 language lexers in lexilla/lexers/)

macOS UI (src/*.mm / src/*.h)
  ├── AppDelegate            – app lifecycle, file open/reopen
  ├── main.mm                – CLI parsing (--help, -n<line>, -lLanguage, etc.)
  ├── MainWindowController   – central window, split view, session management (391 KB)
  ├── EditorView             – Scintilla wrapper + Notepad++ feature layer (264 KB)
  ├── TabManager / NppTabBar – tabbed editing, tab bar rendering
  ├── MenuBuilder            – dynamic menu generation from XML configs
  ├── FindWindow             – search / replace panel
  ├── NppPluginManager       – .dylib plugin loading via dlopen/dlsym
  └── ... (panels, dialogs, helpers — see src/)
```

### Key design points

- **ScintillaView** (from Cocoa Scintilla) is embedded inside **EditorView**, which adds Notepad++ semantics (language detection, fold margin, auto-complete, etc.).
- **MainWindowController** owns both editor panes for the split-view mode and coordinates everything else. It is the largest single file in the repo.
- All persistent user config lives in `~/.notepad++/` at runtime; the app bundle ships defaults under `resources/`.
- XML drives most customisation: `shortcuts.xml`, `contextMenu.xml`, `toolbarButtonsConf.xml`, `langs.model.xml`, `stylers.model.xml`, themes.
- Localisation uses the Windows Notepad++ XML format (137 languages in `resources/localization/`), loaded by **NppLocalizer**.

### Plugin system

Plugins are macOS `.dylib` files placed in `~/.notepad++/plugins/<Name>/<Name>.dylib`. They must export five C symbols: `getName`, `getFuncsArray`, `beNotified`, `messageProc`, and `isUnicode`. Communication happens through NPPM messages passed to Scintilla handles.

### Vendored dependencies

| Directory | Purpose |
|-----------|---------|
| `scintilla/` | Editing engine (do not modify public API surface) |
| `lexilla/` | Syntax lexers; add language support here |

Changes to vendored code should be minimal and clearly marked so they survive upstream merges.

---

## Linux port — next steps (priority / effort order)

### High effort

**Menu bar** — complete set of menus now present: File, Edit, Search, View, Language, Encoding, Settings, Tools, Macro, Run, Plugins, Help. Unimplemented items are `nyi_item()` placeholders (insensitive). Order matches original NPP. Menu items wired so far: File (new/open/reload/save/save-as/save-all/close/close-all/close-all-but/load-session/save-session/quit), Edit (undo/redo/cut/copy/paste/delete/select-all/copy-filepath/copy-filename/copy-dirpath/indent/unindent/column-editor/EOL/datetime/line-ops/blank-ops/case/comment), Search (find/replace/find-in-files/find-next/find-prev/goto/brace/bookmarks/marks/multi-select), View (word-wrap/whitespace/eol/line-nums/fold-margin/bookmarks/edge/folding/fold-current/tab-nav/zoom/always-on-top), Macro (start/stop/play/play-n).

38. ~~**Document List panel**~~ — done: `doclist.c/h`; `GtkListBox` in a `GtkPaned` left of the notebook; toggled from View → Panels → Document List; shows `* filename` for modified docs; close button in header; syncs on tab switch/open/close.
39. ~~**Folder as Workspace panel**~~ — done: `workspace.c/h`; lazy `GtkTreeView` with `GFileEnumerator`; dirs-first sort; folder/file icons; double-click opens file; toggled from View → Panels or File → Open Folder as Workspace…
40. ~~**Function List panel**~~ — done: `funclist.c/h`; 18-language regex parser; 2-level `GtkTreeStore` (class → methods, ungrouped under `(Global)`); brace-depth + Python-indent class membership; 600 ms debounce on `SCN_MODIFIED`; click jumps to line; right-side `GtkPaned`; toggled from View → Panels → Function List.
41. ~~**Document Map**~~ — done: `docmap.c/h`; secondary `ScintillaWidget` sharing document via `SCI_SETDOCPOINTER`; zoom −10; viewport rectangle overlay via `GtkOverlay` + `GtkDrawingArea` + Cairo; scroll sync from `SCN_UPDATEUI`; click-to-navigate; toggled from View → Panels → Document Map.
42. ~~**Search Results panel**~~ — done: `searchresults.c/h`; dockable bottom pane; 3-level `GtkTreeStore` (search → file → hit); accumulates across searches; fed from `findinfiles.c:post_results()`; auto-shown on search completion; double-click navigates; Clear button; toggled from View → Panels → Search Results.
43. ~~**Spell checker**~~ — done: `spell.c/h`; `dlopen` enchant-2 at runtime; indicator 8 squiggle red; 1200 ms debounce; UTF-8 word walk (skip < 3 chars / all-caps); right-click suggestions + Ignore + Add to Dictionary; Settings → Spell Check toggle; graceful fallback when library absent.
44. ~~**Plugin system**~~ — done: `plugin.c/h`; `dlopen`/`dlsym` loader scanning `~/.config/notetux/plugins/<Name>/<Name>.so` + `/usr/lib/notetux/plugins/`; five required exports (`getName`, `getFuncsArray`, `beNotified`, `messageProc`, `isUnicode`) + optional `setInfo(NppData)`; `NppData` carries main window, primary Scintilla widget, and `hostMsg` callback; `plugin_notify_all()` in `on_sci_notify` broadcasts all Scintilla events; NPPM routing for `GETCURRENTSCINTILLA`, `GETNBOPENFILES`, `GETFULLCURRENTPATH`, `GETFILENAME`, `GETDIRECTORYPATH`; auto-generated submenus in Plugins menu (separator + checkmark support); plugin dir auto-created; no build-time dependency on plugins.

### Low effort

Menu items currently `nyi_item()` placeholders that need straightforward implementation — each is a few lines in `main.c` or a small addition to `editor.c`.

45. **About dialog** — `GtkAboutDialog` with version, copyright, GPL-3.0 licence, website, authors and credits (Don Ho / Andrey Letov / Neil Hodgson); triggered from Help → About Notetux++…
46. **Debug Info dialog** — `GtkMessageDialog` showing runtime GTK/GLib versions, compile-time Scintilla/Lexilla versions, and build date (`__DATE__`); triggered from Help → Debug Info…
47. **Project Home Page / Online Documentation** — `gtk_show_uri_on_window` with the GitHub repository URL; two items in Help menu, currently disabled placeholders.
48. **Open in Default Viewer** — `g_app_info_launch_default_for_uri` on the current file's URI; File → Open in Default Viewer; no-op when no file is open.
49. **Open Containing Folder → Terminal** — spawn a terminal emulator in the current file's directory; try `x-terminal-emulator` → `gnome-terminal` → `xfce4-terminal` → `konsole` → `xterm` in order; fall back to home dir when no file is open.
50. **Open Containing Folder → File Manager** — `gtk_show_uri_on_window` with the directory URI; integrates with any XDG-compliant file manager.
51. **On Selection → Open File / Open Folder** — get selected text via `SCI_GETSELTEXT`; strip whitespace; call `editor_open_path` (Open File) or `workspace_set_folder + workspace_set_visible` (Open Folder); no-op on empty selection.
52. **On Selection → Google / Wikipedia / Stack Overflow search** — `g_uri_escape_string` on selection, `gtk_show_uri_on_window` with the encoded query URL; three items in Edit → On Selection submenu.
53. **Read-Only / Clear Read-Only Flag** — `editor_send(SCI_SETREADONLY, 1/0, 0)`; Edit menu items; mark tab label with a lock glyph when active (optional).
54. **Text Direction RTL / LTR** — `editor_send(SCI_SETBIDIRECTIONAL, SC_BIDIRECTIONAL_R2L/L2R, 0)`; View menu items; add `SCI_SETBIDIRECTIONAL 2709` and `SC_BIDIRECTIONAL_*` constants to `sci_c.h`.
55. **Close All to the Left / Right / Unchanged** — iterate the `GtkNotebook` pages in reverse relative to the current tab; call `editor_close_page(p)` for each matched page; three items in File → Close Multiple Documents submenu.
56. **Move to Trash** — `g_file_new_for_path` + `g_file_trash`; on success close the tab via `editor_close_page(-1)`; show error dialog if trash fails; File menu item.
57. **Import Plugin(s)…** — `GtkFileChooserDialog` filtered to `*.so`; copy the selected file into `~/.config/notetux/plugins/<name>/`; show a restart-required notice; Settings → Import → Import Plugin(s)…
58. **Import Style Themes(s)…** — `GtkFileChooserDialog` filtered to `*.xml`; copy into `~/.config/notetux/themes/`; notify user to select via Style Configurator; Settings → Import → Import Style Themes(s)…

### Medium effort

59. **Save a Copy As…** — `GtkFileChooserDialog`, write doc bytes (with encoding conversion via `encoding_from_utf8`) to the new path without updating `NppDoc.filepath`, save point, or tab label; ~40 lines in `editor.c`.
60. **Rename…** — `GtkFileChooserDialog` starting in the current file's directory; `g_rename()` on disk; update `NppDoc.filepath`, restart `GFileMonitor`, refresh tab label and window title; ~50 lines in `editor.c`.
61. **Monitoring (tail -f)** — add `gboolean monitoring` to `NppDoc`; when set, `GFileMonitor` change events auto-reload silently (skip the "do you want to reload?" prompt); toggled from View → Panels → Monitoring (tail -f); `~60` lines across `editor.c` and `main.c`.
62. **Incremental Search** — live search bar (Ctrl+I) docked at the bottom of the editor area; highlights every match as you type using `SCI_SEARCHINTARGET` + `SCI_INDICATORFILLRANGE`; Enter/Shift+Enter jump to next/previous match; Escape closes; `GtkSearchBar` widget; ~100 lines, could live in `findreplace.c` or a new `incremental.c/h`.
63. **Print… / Print Now** — `GtkPrintOperation` with a `GtkPrintContext` that renders the document text line-by-line with syntax-colour approximation (or plain text as a first pass); Print… shows the full dialog, Print Now uses last settings; ~80 lines in `main.c`.

### High effort

64. **Change History** — next/previous/revert/clear per-document change tracking; the simplest approach is a baseline snapshot (bytes at last save) + `SCI_GETCHANGEDLINES` (if available) or a line-level diff against the snapshot on every `SCN_MODIFIED`; clear resets the baseline; navigation jumps the caret to the next changed region.
65. **Project Manager panel** — beyond Folder as Workspace: reads and writes `.nppproject` XML files (file groups, virtual names); dockable left panel; separate `project.c/h`.
66. **Macro management** — Save Current Recorded Macro As… (name + shortcut, persisted to `~/.config/notetux/macros.xml`); Trim Trailing Space and Save; Modify Shortcut / Delete Macro… dialog listing saved macros; extends `macro.c/h`.
67. **Run command dialog** — Run… (execute external command with `%FILE%`/`%DIR%`/`%NAME%`/`%EXT%` substitution; Ctrl+F5); save named commands; Modify Shortcut / Delete Command…; new `run.c/h`.
68. **Plugins Admin dialog** — discover plugins from a GitHub-based manifest (JSON or XML); install / update / remove to `~/.config/notetux/plugins/`; categorised list with description + version; new `pluginsadmin.c/h`; plugs into existing `plugin.c/h` loader.
69. **Clipboard History panel** — track clipboard changes via `GDK_SELECTION_CLIPBOARD` owner-change signal; rolling history of last N text entries; dockable panel (GtkListBox); double-click to paste into active editor; new `cliphistory.c/h`.
70. **Character Panel** — Unicode character browser: code-block tree on the left, character grid on the right; search by name or codepoint; detail card showing character name, UTF-8/16/32 byte sequences; double-click inserts into editor at caret; new `charpanel.c/h`.

### Not planned (Linux-irrelevant or out of scope)

- **Synchronise Scrolling** — requires a split-view mode (not implemented and not planned)
- **Edit Context Menu…** — context menu editor; low value, complex persistence
- **Check for Updates** — native distro packages (.deb/.rpm/etc.) handle updates through the system package manager
- **CommandPalettePanel** — macOS Spotlight metaphor; on Linux, keyboard-shortcut discoverability is the right answer
- **Trim Trailing Space and Save** (standalone macro item) — covered by Macro management (item 66) when implemented as part of that group
