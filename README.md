# Notetux++

**A native Linux text editor inspired by Notepad++.** Built from scratch for Linux with GTK3 — not a port, not Wine.

<p align="center">
  <img src="https://notepad-plus-plus-mac.org/assets/images/icon-128x128.png" alt="Notepad++ for macOS app icon" width="128" height="128">
</p>

---

## Why this is possible

The macOS port and this Linux port share a common foundation: both macOS and Linux are UNIX-based systems. This means the underlying OS primitives — file I/O, process model, character encoding, shared library loading — are compatible. The two vendored libraries at the heart of the editor, **Scintilla** (editing engine) and **Lexilla** (syntax highlighting), already ship GTK3 and platform-agnostic backends alongside their Cocoa ones. The Linux port replaces only the UI layer, swapping Cocoa/Objective-C++ for GTK3/C, while leaving the editor core untouched.

## Features

### Editing
- **Auto-completion** — word and keyword completion triggered after N characters (configurable in Preferences → Editor); sources: language keywords (C/C++, Python, JavaScript, TypeScript, SQL, Rust, Bash, Lua, PHP, Ruby, Perl) and all words in the current document; accept with Tab/Enter, cancel with Escape; enabled/min-chars configurable in Settings → Preferences
- **User-defined languages (UDL)** — custom syntax highlighting via NPP XML definitions in `~/.config/notetux/userDefineLangs/` (user) and `RESOURCES_DIR/userDefineLangs/` (bundled); two bundled Markdown UDLs (light and dark); automatically detected from file extension; available in Language → User Defined Languages submenu; full LexUser.cxx (ported from Windows) handles comments, delimiters, keyword groups, fold markers, operators; multi-word tokens (`"hello world"`), per-style colors/fonts, case-insensitivity, fold-of-comments all supported
- Multi-tab editing with reorderable tabs and close buttons
- File operations: New, Open (multi-file), Save, Save As, Close
- Undo / Redo, Cut / Copy / Paste, Select All
- Go To Line dialog
- Modified-document tracking with ask-to-save on close/quit
- **Session save / restore** — open file set, per-tab caret and scroll positions saved to `~/.config/notetux/session.xml` on quit and restored on next launch; skips files that no longer exist on disk; CLI file arguments suppress restore
- **Auto-backup** — every N seconds (default 60, configurable in Preferences → Backup), any modified unsaved document is written to `~/.config/notetux/backup/<basename>`; backup is removed when the file is cleanly saved or the tab is closed; enabled/interval configurable via Settings → Preferences → Backup
- Command-line file arguments

### Syntax Highlighting
- Automatic language detection from file extension
- 70+ languages via Lexilla: C/C++, Python, JavaScript, TypeScript, PHP, HTML, CSS, JSON, XML, SQL, Bash, Ruby, Perl, Lua, Rust, Go, Java, Swift, Markdown, YAML, TOML, CMake, Makefile, Diff, and many more (Smalltalk, Forth, OScript, AVS, Hollywood, PureBasic, FreeBasic, BlitzBasic, KiX, VisualProlog, BaanC, NNCronTab, CSound, EScript, Spice, …)
- Keyword highlighting with per-language keyword sets
- Code folding with fold margin

### Themes & Style Configurator
- **Style Configurator** dialog (Settings → Style Configurator)
  - 20 bundled themes: Monokai, Bespin, Obsidian, Solarized, Twilight, DarkModeDefault, and more
  - User themes from `~/.config/notetux/themes/` (place any Notepad++ XML theme file there)
  - Per-language, per-style editing: font family, font size, foreground/background color, bold, italic, underline
  - Live preview of each style
  - Save user overrides to `~/.config/notetux/stylers.xml`
- System monospace font auto-detected on first run (via GSettings `org.gnome.desktop.interface`)

### Search
- Find / Replace dialog with forward/backward search, match-case, whole-word options
- Go To Line
- **Find in Files** — Search → Find in Files… (Ctrl+Shift+F): searches files in a chosen directory with optional subdirectory recursion; file filter (e.g. `*.c;*.h`), match-case, whole-word options; results shown in a collapsible two-level tree (file → matching line); double-click a line to open the file and jump directly to it; background `GThread` keeps the UI responsive; pre-fills the search term from the current selection
- **Column / block selection** — rectangular selection via Alt+drag or Alt+Shift+arrows (enabled by `SCVS_RECTANGULARSELECTION | SCVS_USERACCESSIBLE`); **Column Editor** dialog (Edit → Column Editor…, Alt+C): two-mode notebook — "Insert Text" (inserts a string at the selection's left edge on every covered line) and "Insert Number" (inserts an incrementing number sequence: configurable initial value, step, decimal/hex/octal format, optional leading-zero padding); processes lines bottom-to-top to avoid position drift
- **Multi-select** — `SCI_SETMULTIPLESELECTION` + `SCI_SETADDITIONALSELECTIONTYPING` + `SC_MULTIPASTE_EACH` enabled globally; **Select All Occurrences** (Search → Select All Occurrences, Ctrl+Alt+A): selects the word under the cursor if nothing is selected, then calls `SCI_MULTIPLESELECTADDEACH` to highlight every instance simultaneously; **Add Next Occurrence** (Search → Add Next Occurrence, Ctrl+Alt+D): adds the next match as an additional selection one at a time; typing or pasting applies to all selections at once

### Interface
- GTK3 toolbar with Fluent icon set
- Status bar showing line/column, EOL mode (CRLF/CR/LF), encoding, active language, and INS/OVR mode indicator
- Keyboard shortcuts matching Notepad++ conventions (Ctrl+N/O/S/W/Z/Y/X/C/V/A/F/H/G)
- **Language menu** — top-level Language menu to manually override the detected language, grouped into 9 categories (C/C++, Web, Scripting, Systems, Markup/Config, Database, Scientific, Hardware, Other) with radio checkmarks; "Normal Text" at top; checkmark syncs automatically on tab switch
- **EOL Conversion** — Edit → EOL Conversion submenu with radio items: Windows (CR+LF), Unix (LF), Old Mac (CR); converts all existing line endings when switched; status bar EOL cell syncs on tab switch
- **Word wrap** — View → Word Wrap (Alt+Z) toggles word wrap per tab; state is stored per document and restored on tab switch; toolbar wrap button stays in sync
- **Bookmarks** — Search menu: Toggle Bookmark (Ctrl+F2), Next Bookmark (F2), Previous Bookmark (Shift+F2), Clear All Bookmarks; Cut/Copy/Delete Bookmarked Lines; click the bookmarks margin to toggle; blue roundrect marker in margin 1; next/prev wraps around
- **Mark styles** — Search → Mark Styles submenu: 5 color highlight styles (yellow, cyan, blue, orange, magenta) using Scintilla `INDIC_ROUNDBOX` indicators; apply to current selection, clear per-style or all at once, jump to next/previous occurrence of each style
- **Go to matching brace** — Search → Go to Matching Brace (Ctrl+]): moves caret to the matching `()[]{}` or `<>` brace; live `STYLE_BRACELIGHT` / `STYLE_BRACEBAD` highlight on every cursor move via `SCN_UPDATEUI`
- **Recent files list** — File → Open Recent submenu: last 10 opened/saved files, persisted to `~/.config/notetux/recentfiles.txt`; opens on click; Clear Recent Files at bottom of list
- **Show/hide symbols** — View menu check items: Show Whitespace, Show EOL Markers, Show Line Numbers, Show Fold Margin, Show Bookmarks Margin; state is global and applied to all tabs
- **Edge column** — View menu toggle (Show Edge Column) draws a vertical guide line; "Set Edge Column…" opens a dialog to choose the column (default 80, range 1–512)
- **Insert date/time** — Edit → Insert Date/Time submenu: Short format (`HH:MM:SS MM/DD/YYYY`) and Long format (`Weekday, Month DD, YYYY HH:MM:SS`); inserts at cursor, replacing any selection
- **Line operations** — Edit → Line Operations submenu: Duplicate Line (Ctrl+D), Delete Line (Ctrl+Shift+L), Move Line Up/Down (Ctrl+Shift+↑/↓), Join Lines, Split Lines (at edge column, default 80), Insert Blank Line Above (Ctrl+Alt+Enter), Insert Blank Line Below (Ctrl+Shift+Enter), Remove Duplicate Lines, Remove Blank Lines; Sort Lines submenu: Lexicographic, Lexicographic (case-insensitive), By Length, By Number, Random Shuffle, Reverse Order
- **Trim whitespace** — Edit → Blank Operations submenu: Trim Trailing Whitespace, Trim Leading Whitespace, Trim Both; operates on selection or whole document if nothing selected; preserves original EOL characters
- **Hash Generator** — Tools → Hash Generator dialog showing MD5, SHA-1, SHA-256 and SHA-512 of the current selection (or whole document if nothing selected); uses GLib's built-in checksum API, no extra dependencies
- **Base64 / Hex** — Tools menu: Base64 Encode/Decode replaces selection with encoded/decoded text; ASCII→Hex encodes each byte as two hex digits; Hex→ASCII decodes hex pairs back to bytes (whitespace in input is ignored; invalid hex leaves selection unchanged)
- **Case conversion** — Edit → Convert Case To submenu: UPPER CASE, lower case (both via Scintilla native), Proper Case, Sentence case, iNVERT cASE, rAnDoM cAsE; all operate on the current selection
- **Comment / Uncomment** — Edit → Comment/Uncomment submenu: Toggle Single Line Comment (Ctrl+K) and Toggle Block Comment (Ctrl+Shift+K); language-aware delimiters for 80+ languages; toggles (adds/removes) based on whether all covered lines are already commented
- **Whitespace conversions** — Edit → Blank Operations submenu: Convert Spaces to Tabs (replaces leading spaces with tabs respecting the current tab width) and Convert Tabs to Spaces (expands leading tabs to spaces using the current tab width)
- **Encoding selection** — Encoding top-level menu with 17 encodings across 4 regional groups (Western European, Central European, Cyrillic, East Asian); per-tab encoding stored in `NppDoc.encoding`; auto-detected from BOM (UTF-8/16 LE/BE) or UTF-8 validation on open; file bytes converted to UTF-8 for display and back to the chosen encoding on save; statusbar and radio item sync on tab switch
- **Keyboard shortcut mapper** — Settings → Shortcut Mapper: dialog listing all 27 configurable commands (File/Edit/Search); double-click to capture a new key combination; Reset Selected / Reset All; persisted to `~/.config/notetux/shortcuts.xml`; overrides applied at startup before menus are built
- **Preferences dialog** — Settings → Preferences: 4-page dialog (Editor / Display / New Document / General) covering tab width/indentation, auto-indent, brace highlighting, caret style, word wrap, EOL mode, default encoding, title format, and copy-line behaviour; persisted to `~/.config/notetux/config.xml`; settings applied live without restarting
- **Auto-indent** — three modes selectable in Preferences → Editor: None (disabled), Basic (copies leading whitespace of the previous line on Enter), Advanced (Basic + adds one indent level after lines ending with `{` or `:`, and auto-dedents when a `}` is typed at the start of the new line)
- **Code folding controls** — View → Folding submenu: Fold All (Ctrl+Alt+F9), Unfold All (Ctrl+Alt+Shift+F9), and Fold / Unfold Level 1–8 (collapses or expands all fold headers at the chosen nesting level)
- **Macro recording / playback** — Macro menu: Start Recording (Ctrl+Shift+R), Stop Recording, Playback (Ctrl+Shift+P), Run Macro Multiple Times… (dialog with spin button); stores `(msg, wParam, lParam)` triples from `SCN_MACRORECORD`; string-argument messages (REPLACESEL, INSERTTEXT, etc.) are heap-copied for safe replay; each playback wrapped in a single undo action; four toolbar buttons (startrecord / stoprecord / playrecord / playrecord_m) with correct enabled/disabled states
- **File change detection** — `GFileMonitor` watches each open file for external modifications; when the file changes on disk a modal prompt offers to reload (preserving caret position) or keep the current version; changes caused by our own save are suppressed via an `ignore_next_change` flag; the monitor is started on open/save-as and cancelled on tab close
- **Change history / git gutter** — a 4-pixel margin (margin 3) shows per-line diff status against `HEAD`: green (`SC_MARK_FULLRECT`) for added lines, orange for modified lines, red (`SC_MARK_LEFTRECT`) for deleted-line positions; `git diff HEAD -- <file>` runs in a background `GSubprocess`; updates are debounced (800 ms) and triggered on file open, save, and any text modification; unified diff parsed to classify added/modified/deleted ranges
- **Document List panel** — View → Panels → Document List toggles a dockable side panel listing all open tabs; each row shows a `*` prefix for modified documents; clicking a row switches to that tab; the panel syncs automatically when tabs are opened, closed, renamed or switched; housed in a resizable `GtkPaned` to the left of the editor
- **Folder as Workspace panel** — View → Panels → Folder as Workspace (or File → Open Folder as Workspace…) opens a directory tree browser; lazy-loaded `GtkTreeView`: directories expand on demand via `g_file_enumerate_children`; entries sorted directories-first then alphabetically; hidden files skipped; folder/file icons from the system icon theme; double-click opens a file in the editor; current root shown in a path label in the panel header
- **Function List panel** — View → Panels → Function List toggles a dockable right-side panel showing the structure of the current file; per-language regex patterns cover 18 languages (C, C++, ObjC, Python, JavaScript, TypeScript, Java, C#, Go, Rust, PHP, Ruby, Bash, Lua, Swift, Kotlin, Perl, SQL, PowerShell); two-level tree: class/struct nodes at the root, functions as children; ungrouped functions collected under a `(Global)` node; Python uses indentation depth for class membership; brace-depth tracking for all other languages; debounced 600 ms rebuild on each `SCN_MODIFIED`; clicking a row jumps the editor to that line; panel is hidden by default
- **Document Map** — View → Panels → Document Map toggles a dockable minimap panel on the far right; a secondary `ScintillaWidget` shares the same document as the main editor via `SCI_SETDOCPOINTER` (text and syntax-highlighting tokens are automatically mirrored); zoomed to −10 with all margins, scrollbars, and caret hidden; a semi-transparent blue viewport rectangle is painted on top via `GtkOverlay` + `GtkDrawingArea` + Cairo; the minimap centres on the currently visible range on every `SCN_UPDATEUI` event; the overlay captures all pointer events so clicking or dragging the minimap scrolls the main editor continuously; no header — the minimap fills the full panel for seamless integration; panel is hidden by default
- **Search Results panel** — View → Panels → Search Results (also auto-shown after every Find in Files search) toggles a dockable bottom panel; three-level `GtkTreeStore`: search root row ("Search "needle" — N matches in M files") → file rows ("path (N hits)") → hit rows ("line: text"); results accumulate across multiple searches without being cleared; double-clicking a hit row opens the file and jumps to that line; a Clear button wipes all accumulated results; the panel lives in a vertical `GtkPaned` below the main editor area; hidden by default, shown automatically when a Find in Files search returns results
- **Spell checker** — Settings → Spell Check enables inline spell checking; `libenchant-2` loaded at runtime via `dlopen` (gracefully disabled if not installed); dictionary selected from system locale (`LC_MESSAGES`) with fallback to base language; misspelled words underlined with a red squiggle (Scintilla indicator 8, `INDIC_SQUIGGLE`); UTF-8-aware word walker skips words under 3 characters and all-uppercase acronyms; full-document pass limited to 200 KB, debounced 1200 ms after each edit; right-clicking a misspelled word shows a context menu with up to 8 suggestions (click to replace), "Ignore Word" (session), and "Add to Dictionary" (permanent)
- **Plugin system** — plugins are native Linux shared libraries (`.so`) placed in `~/.config/notetux/plugins/<Name>/<Name>.so`; each plugin exports five standard symbols (`getName`, `getFuncsArray`, `beNotified`, `messageProc`, `isUnicode`) plus an optional `setInfo(NppData)` to receive host handles and a host-message callback; the host calls `beNotified` on every Scintilla editor event; plugins query the host via `NppData.hostMsg(NPPM_*, ...)` — supported messages: `NPPM_GETCURRENTSCINTILLA`, `NPPM_GETNBOPENFILES`, `NPPM_GETFULLCURRENTPATH`, `NPPM_GETFILENAME`, `NPPM_GETDIRECTORYPATH`; each plugin's functions appear as a submenu under the Plugins menu; separator items (`"-"`) and checkbox items (`init2Check != 0`) are supported; a minimal example plugin is provided in `linux/example_plugin/HelloPlugin/`

### Localisation
- Automatic system locale detection via GLib (`g_get_language_names()`)
- 137 bundled translations from the official Notepad++ XML localization files
- All menus, dialogs, and buttons translated; falls back to English when no match

## Build

Requires CMake 3.20+, GCC or Clang, and GTK3 development headers.

```sh
sudo apt-get install libgtk-3-dev cmake build-essential
cmake -B linux/build -S linux
cmake --build linux/build -j$(nproc)
```

Output: `linux/build/notetux++`

## Run

```sh
./linux/build/notetux++
./linux/build/notetux++ file1.c file2.h
```

## Development methodology

### Goal: feature parity with the macOS port

The long-term target is to reach the same set of functionalities as the [Notepad++ for macOS](https://github.com/notepadplusplus/notepad-plus-plus-mac) port — or as close to it as the platform allows. Some features are macOS-specific (floating panels, Command Palette, Spotlight integration) and will not be ported; everything else is fair game.

Feature parity is tracked by cross-referencing:
1. **The macOS source files** (`src/*.mm`) — each panel, dialog and controller is a candidate for porting.
2. **The Linux menu stubs** — every `nyi_item()` placeholder in `linux/src/main.c` is an unimplemented menu item from the original Notepad++ feature set.

### How new features are tackled

Each development session follows the same pattern used to implement items 38–44 (the side panels, spell checker and plugin system):

1. **Audit** — compare the macOS source and the Linux `nyi_item()` stubs to find what is missing.
2. **Classify by effort** — group items into *low* (trivial callbacks, a few lines), *medium* (new dialogs or editor functions, ~50–100 lines), and *high* (entirely new panels or subsystems, their own `.c/.h` files).
3. **Implement in effort order** — low-effort items first (they accumulate quickly and keep the app feeling complete), then medium, then high.
4. **Update docs** — CLAUDE.md and this README are updated when each item is completed, so the lists always reflect the current state.

The numbered items below (45–70) are the current open list. Completed items are struck through. Items with no number are not planned.

## Upcoming features

Ordered by implementation effort (low → high). The core editing experience is complete; remaining items are polish, integration, and advanced features.

### Low effort

| # | Feature | Description |
|---|---------|-------------|
| ~~45~~ | ~~About dialog~~ | ~~`GtkAboutDialog` with version, copyright, GPL-3.0, credits~~ ✓ |
| ~~46~~ | ~~Debug Info dialog~~ | ~~Runtime GTK/GLib + compile-time Scintilla/Lexilla versions~~ ✓ |
| ~~47~~ | ~~Project Home Page / Online Documentation~~ | ~~Open GitHub repository and README in the system browser~~ ✓ |
| ~~48~~ | ~~Open in Default Viewer~~ | ~~Open current file with its registered default application~~ ✓ |
| ~~49~~ | ~~Open Containing Folder → Terminal~~ | ~~Spawn terminal emulator in current file's directory~~ ✓ |
| ~~50~~ | ~~Open Containing Folder → File Manager~~ | ~~Open current file's directory in the system file manager~~ ✓ |
| ~~51~~ | ~~On Selection → Open File / Open Folder~~ | ~~Treat selected text as a file path or directory and open it~~ ✓ |
| ~~52~~ | ~~On Selection → Web searches~~ | ~~Google, Wikipedia, Stack Overflow — URL-encode selection and open in browser~~ ✓ |
| ~~53~~ | ~~Read-Only / Clear Read-Only Flag~~ | ~~Toggle `SCI_SETREADONLY` on the active document~~ ✓ |
| ~~54~~ | ~~Text Direction RTL / LTR~~ | ~~`SCI_SETBIDIRECTIONAL` for right-to-left and left-to-right editing~~ ✓ |
| ~~55~~ | ~~Close All to the Left / Right / Unchanged~~ | ~~Bulk-close tabs relative to the current one, or close all unmodified tabs~~ ✓ |
| ~~56~~ | ~~Move to Trash~~ | ~~Send current file to the system trash and close its tab~~ ✓ |
| ~~57~~ | ~~Import Plugin(s)…~~ | ~~File chooser to copy a `.so` plugin into `~/.config/notetux/plugins/`~~ ✓ |
| ~~58~~ | ~~Import Style Themes(s)…~~ | ~~File chooser to copy an XML theme into `~/.config/notetux/themes/`~~ ✓ |

### Medium effort

| # | Feature | Description |
|---|---------|-------------|
| ~~59~~ | ~~Save a Copy As…~~ ✓ | Save the document to a new path without switching the active filepath |
| ~~60~~ | ~~Rename…~~ ✓ | Rename the file on disk and update the tab label, title bar and file monitor |
| ~~61~~ | ~~Monitoring (tail -f)~~ ✓ | Auto-reload the current file silently when it changes on disk (no prompt) |
| ~~62~~ | ~~Incremental Search~~ ✓ | Live search bar (Ctrl+I) that highlights matches as you type; Enter to step through results |
| ~~63~~ | ~~Print… / Print Now~~ ✓ | `GtkPrintOperation` with full dialog or immediate print using last settings |

### High effort

| # | Feature | Description |
|---|---------|-------------|
| ~~64~~ | ~~Change History~~ ✓ | Margin-4 bar (gold=unsaved, green=saved); Next/Prev/Revert/Clear via Search menu |
| ~~65~~ | ~~Project Manager panel~~ ✓ | `.nppproject` XML read/write; GtkTreeView with folders/files; leftmost dockable panel |
| ~~66~~ | ~~Macro management~~ ✓ | Named macros in `macros.xml`; Save/Delete dialog; Trim Trailing Space and Save |
| ~~67~~ | ~~Run command dialog~~ ✓ | `%FILE%/%DIR%/%NAME%/%EXT%` substitution; saved commands in `commands.xml`; Ctrl+F5 |
| ~~68~~ | ~~Plugins Admin dialog~~ ✓ | Scan installed plugins; Install from file; Uninstall; restart notice |
| ~~69~~ | ~~Clipboard History panel~~ ✓ | `owner-change` tracking; rolling 20-entry GQueue; double-click pastes into editor |
| ~~70~~ | ~~Character Panel~~ ✓ | 49-block Unicode browser; 16-wide grid; U+ search; insert on click; UTF-8 detail card |

## Release packaging

Once the project is feature-complete, pre-compiled packages will be produced for all major Linux distributions:

| Format | Target distros |
|--------|---------------|
| `.deb` | Debian, Ubuntu, Linux Mint, Pop!_OS |
| `.rpm` | Fedora, RHEL, CentOS Stream, openSUSE |
| `.pkg.tar.zst` | Arch Linux, Manjaro |
| `.apk` | Alpine Linux |

> **Note:** No AppImage, Flatpak, or Snap packages will ever be produced. These distribution formats are philosophically opposed (imho) to how native software should be shipped on Linux — they sidestep the distro package manager, bloat the install, and erode the integration that makes a native app feel native. Packages will be built for distro toolchains only.

## Extra features

Features beyond the original Notepad++ scope, specific to this Linux port. These will be tackled only after all upcoming features are complete.

- **Vim mode** — modal editing (Normal / Insert / Visual) with core Vim motions and commands, toggled via Settings → Vim Mode
- **Terminal panel** — Show a terminal emulator inside the bottom panel with two main functions:
    - if the file or the workspace folder is local, open the terminal in the `cwd`;
    - if the file is remote (once will be available nppFTP plugin)  open an SSH terminal on the connected server.

## Bug fixes

| # | Component | Description | Resolution |
|---|-----------|-------------|------------|
| 1 | Style Configurator | Save and Close buttons had no effect on GTK3/Wayland | Replaced `gtk_dialog_run()` loop with `response` signal handler; dialog is now a persistent singleton closed via `gtk_widget_hide()` |
| 2 | Style Configurator | "Salva" and "Salva e chiudi" actions were swapped — Save closed the dialog, Save and Close kept it open | Corrected response-ID assignment to match translated button labels |
| 3 | Style store | `~/.config/notetux/stylers.xml` triggered a parse warning for entries whose `name` attribute contains `&` (e.g. CaML `BUILTIN FUNC & TYPE`) | XML is pre-processed to escape bare `&` before passing to `GMarkupParser` |
| 4 | Margins | Line numbers and fold +/− indicators never appeared | `SCI_SETMARGINWIDTHN` in `sci_c.h` was defined as **2243** (`SCI_GETMARGINWIDTHN`) instead of **2242**; every margin-width call silently returned a value without setting anything, leaving all margins at default width 0 |

## User configuration

All user data lives in `~/.config/notetux/`:

| Path | Purpose |
|------|---------|
| `~/.config/notetux/stylers.xml` | Saved style/color overrides from the Style Configurator |
| `~/.config/notetux/themes/` | User-supplied theme XML files (Notepad++ format) |
| `~/.config/notetux/recentfiles.txt` | Recently opened/saved files (one path per line, max 10) |
| `~/.config/notetux/shortcuts.xml` | User-defined keyboard shortcut overrides (Notepad++ format) |
| `~/.config/notetux/config.xml` | Preferences (tab width, indent, caret, EOL, encoding, display options) |
| `~/.config/notetux/session.xml` | Session state: open file paths, caret and scroll positions, active tab |
| `~/.config/notetux/backup/` | Auto-backup copies of unsaved/modified documents |
| `~/.config/notetux/userDefineLangs/` | User-defined language XML files (NPP UDL format) |
| `~/.config/notetux/plugins/` | Plugin directory; each plugin lives in `<Name>/<Name>.so` |

## Architecture

```
linux/src/main.c            — GtkApplication, window, menu bar, keyboard shortcuts
linux/src/editor.c/h       — tab/document management, file I/O, Scintilla wrappers
linux/src/statusbar.c/h    — bottom status bar
linux/src/toolbar.c/h      — GTK3 toolbar
linux/src/findreplace.c/h  — Find/Replace dialog
linux/src/lexer.c/h        — language detection, Lexilla integration, keyword tables
linux/src/lexilla_bridge.cpp — C++ bridge: exposes CreateLexer() to C code
linux/src/stylestore.c/h   — theme/style parser and Scintilla style applicator
linux/src/styleeditor.c/h  — Style Configurator dialog
linux/src/encoding.c/h     — encoding table, BOM detection, UTF-8 conversion helpers
linux/src/shortcutmap.c/h  — shortcut table, key-capture dialog, Shortcut Mapper
linux/src/prefs.c/h        — preferences struct, load/save, Preferences dialog
linux/src/udl.c/h          — User Defined Language manager: XML parse, udl_apply()
linux/src/gitgutter.c/h    — Git gutter: background diff, unified diff parser, Scintilla margin markers
linux/src/session.c/h      — Session save/restore: tab set, caret and scroll positions → ~/.config/notetux/session.xml
linux/src/backup.c/h       — Auto-backup: periodic g_timeout writes modified docs to ~/.config/notetux/backup/
linux/src/doclist.c/h      — Document List panel: GtkListBox synced to notebook pages
linux/src/workspace.c/h    — Folder as Workspace panel: lazy GtkTreeView backed by GFileEnumerator
linux/src/funclist.c/h     — Function List panel: per-language regex parser, 2-level GtkTreeStore, debounced rebuild
linux/src/docmap.c/h       — Document Map panel: shared-document minimap ScintillaWidget, viewport Cairo overlay
linux/src/searchresults.c/h — Search Results panel: 3-level GtkTreeStore, fed from findinfiles, bottom-docked
linux/src/spell.c/h         — Spell checker: dlopen enchant-2, indicator squiggle, right-click suggestions
linux/src/plugin.c/h        — Plugin system: .so loader, NppData/hostMsg, beNotified broadcast, NPPM routing, menu generation
linux/src/sci_c.h           — C-safe Scintilla interface

scintilla/                  — vendored editing engine (GTK3 backend used as-is)
lexilla/                    — vendored syntax highlighting (~80 language lexers)
resources/                  — shared with macOS port: themes, stylers.model.xml, langs.model.xml
```

The application layer is pure C (C11). Only `lexilla_bridge.cpp` uses C++ to call the Lexilla `CreateLexer()` API via a single `extern "C"` function. Scintilla and Lexilla are compiled as C++ static libraries and accessed exclusively through their C message API (`scintilla_send_message`).

## A note on the name

> This project was renamed from *Notepad++ for Linux* to **Notetux++** after reading that Don Ho — who publicly professes to be in favour of open-source software and explicitly acknowledges the GPL licence as extremely liberal and open to any kind of fork or port — has nonetheless waged a personal crusade against Andrey Letov, whose only offence was creating a native macOS port under the slightly modified name *Notepad++ for Mac*, using a similar icon.
>
> As an OSS developer, I will always stand behind the work of people like Andrey, and I genuinely hope that some collaboration between us can emerge one day.
>
> Notepad++ as a project has been stagnant for too long. For years — perhaps decades — the community has asked for ports to other platforms. Nothing of the sort has ever materialised. From that inaction Andrey's project was born, and from a fork of Andrey's project, mine was born: **Notetux++**.
>
> The application will have its own icon soon, too.
>
> Despite everything, full credit goes to Don Ho for Notepad++ and the remarkable work behind it — a project that has genuinely shaped how millions of developers edit text. I disagree completely with the crusade he has been waging, and the justifications offered for it are, frankly, implausible. But the application itself deserves its place in the history of software.
>
> <sub>One can't help but wonder: had the energy spent on legal threats been spent on collaboration instead, there might already be an official, cross-platform Notepad++ today — built together by the very people who love the project most. A missed opportunity, perhaps.</sub>
>
> <sub>**Further reading:** Don Ho's own account of the dispute — [Notepad++ Trademark Infringement](https://notepad-plus-plus.org/news/npp-trademark-infringement/) and the follow-up [Clarification on Notepad++ Trademark Infringement](https://notepad-plus-plus.org/news/clarify-npp-trademark-infringement/) — are worth reading and forming your own opinion on.</sub>

## Credits

Notetux++ would not exist without the shoulders it stands on.

**[Notepad++ (Windows)](https://notepad-plus-plus.org)** — created by Don Ho. The original application that started it all: a fast, lightweight, extensible text editor that has been a reference point for developers on Windows for over two decades. The feature set, UX conventions, configuration format, and overall vision of Notetux++ are directly inspired by this work.

**[Notepad++ for macOS](https://github.com/notepadplusplus/notepad-plus-plus-mac)** — created by Andrey Letov. The native macOS port that proved the Notepad++ experience could live outside Windows without Wine or emulation. Notetux++ was forked from this project and inherits its architecture, vendored Scintilla/Lexilla integration, and the original C++/Objective-C++ codebase that Andrey built. Without his work, this Linux port would not exist.

**[Scintilla](https://www.scintilla.org)** — the editing engine powering all three projects. Written by Neil Hodgson.

**[Lexilla](https://www.scintilla.org/Lexilla.html)** — the syntax highlighting library, also by Neil Hodgson, shipping ~80 language lexers used unchanged in Notetux++.
