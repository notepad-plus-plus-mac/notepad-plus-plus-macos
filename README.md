# Notepad++ for Linux

**The first native port of Notepad++ to Linux.** A full port of the original [Notepad++](https://notepad-plus-plus.org) codebase — not a rewrite, not an alternative, not Wine.

<p align="center">
  <img src="https://notepad-plus-plus-mac.org/assets/images/icon-128x128.png" alt="Notepad++ for macOS app icon" width="128" height="128">
</p>

---

## Why this is possible

The macOS port and this Linux port share a common foundation: both macOS and Linux are UNIX-based systems. This means the underlying OS primitives — file I/O, process model, character encoding, shared library loading — are compatible. The two vendored libraries at the heart of the editor, **Scintilla** (editing engine) and **Lexilla** (syntax highlighting), already ship GTK3 and platform-agnostic backends alongside their Cocoa ones. The Linux port replaces only the UI layer, swapping Cocoa/Objective-C++ for GTK3/C, while leaving the editor core untouched.

## Features

### Editing
- **Auto-completion** — word and keyword completion triggered after N characters (configurable in Preferences → Editor); sources: language keywords (C/C++, Python, JavaScript, TypeScript, SQL, Rust, Bash, Lua, PHP, Ruby, Perl) and all words in the current document; accept with Tab/Enter, cancel with Escape; enabled/min-chars configurable in Settings → Preferences
- **User-defined languages (UDL)** — custom syntax highlighting via NPP XML definitions in `~/.config/npp/userDefineLangs/` (user) and `RESOURCES_DIR/userDefineLangs/` (bundled); two bundled Markdown UDLs (light and dark); automatically detected from file extension; available in Language → User Defined Languages submenu; full LexUser.cxx (ported from Windows) handles comments, delimiters, keyword groups, fold markers, operators; multi-word tokens (`"hello world"`), per-style colors/fonts, case-insensitivity, fold-of-comments all supported
- Multi-tab editing with reorderable tabs and close buttons
- File operations: New, Open (multi-file), Save, Save As, Close
- Undo / Redo, Cut / Copy / Paste, Select All
- Go To Line dialog
- Modified-document tracking with ask-to-save on close/quit
- **Session save / restore** — open file set, per-tab caret and scroll positions saved to `~/.config/npp/session.xml` on quit and restored on next launch; skips files that no longer exist on disk; CLI file arguments suppress restore
- Command-line file arguments

### Syntax Highlighting
- Automatic language detection from file extension
- 70+ languages via Lexilla: C/C++, Python, JavaScript, TypeScript, PHP, HTML, CSS, JSON, XML, SQL, Bash, Ruby, Perl, Lua, Rust, Go, Java, Swift, Markdown, YAML, TOML, CMake, Makefile, Diff, and many more (Smalltalk, Forth, OScript, AVS, Hollywood, PureBasic, FreeBasic, BlitzBasic, KiX, VisualProlog, BaanC, NNCronTab, CSound, EScript, Spice, …)
- Keyword highlighting with per-language keyword sets
- Code folding with fold margin

### Themes & Style Configurator
- **Style Configurator** dialog (Settings → Style Configurator)
  - 20 bundled themes: Monokai, Bespin, Obsidian, Solarized, Twilight, DarkModeDefault, and more
  - User themes from `~/.config/npp/themes/` (place any Notepad++ XML theme file there)
  - Per-language, per-style editing: font family, font size, foreground/background color, bold, italic, underline
  - Live preview of each style
  - Save user overrides to `~/.config/npp/stylers.xml`
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
- **Recent files list** — File → Open Recent submenu: last 10 opened/saved files, persisted to `~/.config/npp/recentfiles.txt`; opens on click; Clear Recent Files at bottom of list
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
- **Keyboard shortcut mapper** — Settings → Shortcut Mapper: dialog listing all 27 configurable commands (File/Edit/Search); double-click to capture a new key combination; Reset Selected / Reset All; persisted to `~/.config/npp/shortcuts.xml`; overrides applied at startup before menus are built
- **Preferences dialog** — Settings → Preferences: 4-page dialog (Editor / Display / New Document / General) covering tab width/indentation, auto-indent, brace highlighting, caret style, word wrap, EOL mode, default encoding, title format, and copy-line behaviour; persisted to `~/.config/npp/config.xml`; settings applied live without restarting
- **Auto-indent** — three modes selectable in Preferences → Editor: None (disabled), Basic (copies leading whitespace of the previous line on Enter), Advanced (Basic + adds one indent level after lines ending with `{` or `:`, and auto-dedents when a `}` is typed at the start of the new line)
- **Code folding controls** — View → Folding submenu: Fold All (Ctrl+Alt+F9), Unfold All (Ctrl+Alt+Shift+F9), and Fold / Unfold Level 1–8 (collapses or expands all fold headers at the chosen nesting level)
- **Change history / git gutter** — a 4-pixel margin (margin 3) shows per-line diff status against `HEAD`: green (`SC_MARK_FULLRECT`) for added lines, orange for modified lines, red (`SC_MARK_LEFTRECT`) for deleted-line positions; `git diff HEAD -- <file>` runs in a background `GSubprocess`; updates are debounced (800 ms) and triggered on file open, save, and any text modification; unified diff parsed to classify added/modified/deleted ranges

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

Output: `linux/build/notepad++`

## Run

```sh
./linux/build/notepad++
./linux/build/notepad++ file1.c file2.h
```

## Upcoming features

Ordered by implementation effort (low → high).

> **Note:** All the features with low and medium effort required are marked as completed. No intermediate release are planned. This software will be released when all the points in this list will be successfully completed.

### High effort
- **Auto-backup** — timed backup copies to `~/.config/npp/backup/`
- **File change detection** — detect external modifications and prompt to reload
- **Macro recording / playback** — record and replay keystroke sequences
- **Document List panel** — dockable panel listing all open tabs
- **Folder as Workspace panel** — multi-root file tree browser
- **Function List panel** — tree view of functions/classes in the current file
- **Document Map** — minimap preview of the full document
- **Search Results panel** — accumulated find results with navigation
- **Spell checker** — inline spell checking with highlight and correction
- **Plugin system** — dlopen-based plugin loading, menu integration, NPPM message routing

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

## Bug fixes

| # | Component | Description | Resolution |
|---|-----------|-------------|------------|
| 1 | Style Configurator | Save and Close buttons had no effect on GTK3/Wayland | Replaced `gtk_dialog_run()` loop with `response` signal handler; dialog is now a persistent singleton closed via `gtk_widget_hide()` |
| 2 | Style Configurator | "Salva" and "Salva e chiudi" actions were swapped — Save closed the dialog, Save and Close kept it open | Corrected response-ID assignment to match translated button labels |
| 3 | Style store | `~/.config/npp/stylers.xml` triggered a parse warning for entries whose `name` attribute contains `&` (e.g. CaML `BUILTIN FUNC & TYPE`) | XML is pre-processed to escape bare `&` before passing to `GMarkupParser` |
| 4 | Margins | Line numbers and fold +/− indicators never appeared | `SCI_SETMARGINWIDTHN` in `sci_c.h` was defined as **2243** (`SCI_GETMARGINWIDTHN`) instead of **2242**; every margin-width call silently returned a value without setting anything, leaving all margins at default width 0 |

## User configuration

All user data lives in `~/.config/npp/`:

| Path | Purpose |
|------|---------|
| `~/.config/npp/stylers.xml` | Saved style/color overrides from the Style Configurator |
| `~/.config/npp/themes/` | User-supplied theme XML files (Notepad++ format) |
| `~/.config/npp/recentfiles.txt` | Recently opened/saved files (one path per line, max 10) |
| `~/.config/npp/shortcuts.xml` | User-defined keyboard shortcut overrides (Notepad++ format) |
| `~/.config/npp/config.xml` | Preferences (tab width, indent, caret, EOL, encoding, display options) |
| `~/.config/npp/session.xml` | Session state: open file paths, caret and scroll positions, active tab |
| `~/.config/npp/userDefineLangs/` | User-defined language XML files (NPP UDL format) |

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
linux/src/session.c/h      — Session save/restore: tab set, caret and scroll positions → ~/.config/npp/session.xml
linux/src/sci_c.h           — C-safe Scintilla interface

scintilla/                  — vendored editing engine (GTK3 backend used as-is)
lexilla/                    — vendored syntax highlighting (~80 language lexers)
resources/                  — shared with macOS port: themes, stylers.model.xml, langs.model.xml
```

The application layer is pure C (C11). Only `lexilla_bridge.cpp` uses C++ to call the Lexilla `CreateLexer()` API via a single `extern "C"` function. Scintilla and Lexilla are compiled as C++ static libraries and accessed exclusively through their C message API (`scintilla_send_message`).

## Original projects

- [Notepad++ for macOS](https://github.com/notepadplusplus/notepad-plus-plus-mac)
- [Scintilla](https://www.scintilla.org)
- [Lexilla](https://www.scintilla.org/Lexilla.html)
- [Notepad++ (Windows)](https://notepad-plus-plus.org)
