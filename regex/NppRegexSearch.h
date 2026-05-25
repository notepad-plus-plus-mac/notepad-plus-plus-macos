// SPDX-License-Identifier: MIT
//
// Drop-in replacement for Scintilla's default BuiltinRegex backend, providing
// the `SCFIND_REGEXP_EMPTYMATCH_*` and `SCFIND_REGEXP_SKIPCRLFASONE` flags
// that Windows Notepad++'s boost-based backend exposes. Selected via Scintilla's
// own SCI_OWNREGEX extension point — no patches to upstream Scintilla.
//
// Engine: std::regex (ECMAScript flavor) from libc++ — the same engine
// Scintilla's Cxx11RegexFindText already uses. The added value here is the
// stateful continuation tracking that lets Replace-All / Find-All / Mark-All
// / Count terminate cleanly on zero-width matches (`$`, `^`, `\b`).
//
// Bit layout matches Windows boostregex/BoostRegexSearch.h so plugins built
// against the Windows plugin headers send the correct flags by accident.

#ifndef NPP_REGEX_SEARCH_H
#define NPP_REGEX_SEARCH_H

#define SCFIND_REGEXP_DOTMATCHESNL              0x10000000
#define SCFIND_REGEXP_EMPTYMATCH_MASK           0xE0000000
#define SCFIND_REGEXP_EMPTYMATCH_NONE           0x00000000
#define SCFIND_REGEXP_EMPTYMATCH_NOTAFTERMATCH  0x20000000
#define SCFIND_REGEXP_EMPTYMATCH_ALL            0x40000000
#define SCFIND_REGEXP_EMPTYMATCH_ALLOWATSTART   0x80000000
#define SCFIND_REGEXP_SKIPCRLFASONE             0x08000000

#endif // NPP_REGEX_SEARCH_H
