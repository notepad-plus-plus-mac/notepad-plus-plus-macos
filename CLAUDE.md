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
./build/notepad++
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
| `stylestore.c/h` | Parses `stylers.model.xml` / user `~/.config/npp/stylers.xml`; applies Scintilla styles |
| `styleeditor.c/h` | Style Configurator dialog (theme picker, per-language style editing) |
| `sci_c.h` | C-safe Scintilla constants and `SCNotification` layout |
| `encoding.c/h` | Encoding table (17 encodings), BOM detection, `g_convert` wrappers for open/save |
| `shortcutmap.c/h` | Shortcut table (27 entries), XML load/save, Shortcut Mapper dialog with key-capture |
| `prefs.c/h` | Preferences struct (`NppPrefs`), XML load/save, 4-page Preferences dialog |
| `findinfiles.c/h` | Find in Files dialog: directory walk, GThread search, collapsible GtkTreeView results |
| `columneditor.c/h` | Column Editor dialog: insert text or number sequence into each line of a selection |
| `autocomplete.c/h` | Word+keyword auto-completion: `SCI_AUTOCSHOW` driven by `SCN_CHARADDED`; sources keywords from `lexer_get_keywords()` and scans the document (first 100 KB) |
| `udl.c/h` | User Defined Language manager: scan and parse NPP UDL XML files; `udl_apply()` routes 28 kwlist slots to `SCI_SETPROPERTY` (comments, numbers, operators1, folders-in-code1, delimiters) or `SCI_SETKEYWORDS` (operators2, folders-in-code2/comment, keywords1-8); applies per-style colors/fonts; multi-word tokens preprocessed (`"a b"` → `a\vb`, `'a b'` → `a\bb`) |

**User config location (Linux port):** `~/.config/npp/`
- `stylers.xml` — user style overrides (saved by Style Configurator)
- `themes/` — user theme XML files (scanned alongside bundled `resources/themes/`)
- `recentfiles.txt` — recently opened/saved files (one path per line, max 10)
- `shortcuts.xml` — user keyboard shortcut overrides
- `config.xml` — preferences (tab width, indent, caret, EOL, encoding, display options)
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

**Known bugs fixed:**
- `stylestore`: `GMarkupParser` rejected `~/.config/npp/stylers.xml` at lines containing `&` in attribute values (e.g. CaML `BUILTIN FUNC & TYPE`). Fixed in `stylestore.c:fix_bare_ampersands()` by escaping bare `&` before parsing.
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

29. **Change history / git gutter** — run `git diff` in background; parse unified diff; set `SCI_MARKERDEFINE` symbols in margin.
34. **Session save / restore** — serialize open file paths + scroll/caret positions to `~/.config/npp/session.xml`; restore on launch.
35. **Auto-backup** — `g_timeout_add_seconds()` writes current doc to `~/.config/npp/backup/<name>~`; clean on clean save.
36. **File change detection** — `GFileMonitor` on each open path; prompt reload on `G_FILE_MONITOR_EVENT_CHANGED`.
37. **Macro recording / playback** — hook `SCN_MACRORECORD`; store `(msg, wParam, lParam)` triples; replay with `SCI_SENDMESSAGE`.
38. **Document List panel** — dockable `GtkListBox` synced to notebook pages; click to switch tab.
39. **Folder as Workspace panel** — dockable `GtkTreeView` backed by `GFileEnumerator`; double-click opens file.
40. **Function List panel** — dockable `GtkTreeView`; parse current file with a per-language regex or ctags; update on `SCN_MODIFIED`.
41. **Document Map** — secondary `ScintillaWidget` in read-only mode tracking the main one; scale via `SCI_SETZOOM`.
42. **Search Results panel** — dockable `GtkTreeView` accumulating Find-in-Files hits; click to navigate.
43. **Spell checker** — integrate `enchant-2` library; walk words with `SCI_WORDSTARTPOSITION`/`SCI_WORDENDPOSITION`; mark with indicator.
44. **Plugin system** — `dlopen`/`dlsym` loader for `.so` plugins exporting the five NPP symbols; `NPPM_*` message routing; auto-generated plugin menu.
