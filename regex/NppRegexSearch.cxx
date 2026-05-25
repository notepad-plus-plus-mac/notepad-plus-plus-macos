// SPDX-License-Identifier: MIT
//
// NppRegexSearch — Scintilla RegexSearchBase implementation that mirrors the
// behavior of Notepad++ Windows's Boost-based regex backend, but on top of
// std::regex (libc++ ECMAScript). Selected via Scintilla's SCI_OWNREGEX
// extension point (the same hook Windows uses for boostregex/), so no
// patches to upstream Scintilla.
//
// What this gives us over the default BuiltinRegex:
//   1. SCFIND_REGEXP_EMPTYMATCH_NOTAFTERMATCH — reject zero-width matches
//      at the continuation start position. Without this, a "Replace All"
//      of `$` → `X` on a file ending in `\n` runs to its 100 000-iteration
//      cap because the same `$` keeps re-matching as the cursor walks
//      through the inserted X's. See macOS issue #151.
//   2. SCFIND_REGEXP_EMPTYMATCH_ALL / ALLOWATSTART — Windows uses these to
//      tune single-Find vs Replace vs Find-Next-For-Replace semantics.
//      We honor them so behavior matches Notepad++ Windows exactly.
//   3. SCFIND_REGEXP_SKIPCRLFASONE — when advancing past a rejected empty
//      match, treat CRLF as one user-perceived character.
//
// Continuation tracking uses Scintilla's DocWatcher mechanism (subscribing
// via Document::AddWatcher) so we observe replacements made by the host
// between successive FindText calls — replicating BoostRegexSearch's Match
// class.

// STL — must come before Document.h, which itself uses std::map/optional/etc.
// without including them, expecting the TU to have already done so.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <chrono>
#include <forward_list>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ScintillaTypes.h"
#include "ILoader.h"
#include "ILexer.h"
#include "Debugging.h"
#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "CharClassify.h"
#include "CaseFolder.h"
#include "Decoration.h"
#include "Document.h"
#include "UniConversion.h"

#include "NppRegexSearch.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

// ────────────────────────────────────────────────────────────────────────
// Per-line iterators duplicated from Document.cxx's BuiltinRegex (the
// originals live in an anonymous namespace and aren't externally visible).
// std::regex on libc++/libstdc++ doesn't reliably support `^` and `$` in
// multiline mode, so the search walks the document one Scintilla line at
// a time, exactly like upstream BuiltinRegex.
// ────────────────────────────────────────────────────────────────────────

class RESearchRange {
public:
    int increment;
    Sci::Position startPos;
    Sci::Position endPos;
    Sci::Line lineRangeStart;
    Sci::Line lineRangeEnd;
    Sci::Line lineRangeBreak;
    RESearchRange(const Document *doc, Sci::Position minPos, Sci::Position maxPos) noexcept {
        increment = (minPos <= maxPos) ? 1 : -1;
        startPos = doc->MovePositionOutsideChar(minPos, 1, true);
        endPos   = doc->MovePositionOutsideChar(maxPos, 1, true);
        lineRangeStart = doc->SciLineFromPosition(startPos);
        lineRangeEnd   = doc->SciLineFromPosition(endPos);
        lineRangeBreak = lineRangeEnd + increment;
    }
    Range LineRange(Sci::Line line, Sci::Position lineStartPos, Sci::Position lineEndPos) const noexcept {
        Range range(lineStartPos, lineEndPos);
        if (increment == 1) {
            if (line == lineRangeStart) range.start = startPos;
            if (line == lineRangeEnd)   range.end   = endPos;
        } else {
            if (line == lineRangeEnd)   range.start = endPos;
            if (line == lineRangeStart) range.end   = startPos;
        }
        return range;
    }
};

class ByteIterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type        = char;
    using difference_type   = ptrdiff_t;
    using pointer           = char *;
    using reference         = char &;

    const Document *doc;
    Sci::Position position;

    explicit ByteIterator(const Document *doc_ = nullptr, Sci::Position position_ = 0) noexcept
        : doc(doc_), position(position_) {}
    char operator*() const noexcept { return doc->CharAt(position); }
    ByteIterator &operator++() noexcept { position++; return *this; }
    ByteIterator  operator++(int) noexcept { ByteIterator r(*this); position++; return r; }
    ByteIterator &operator--() noexcept { position--; return *this; }
    bool operator==(const ByteIterator &o) const noexcept { return doc == o.doc && position == o.position; }
    bool operator!=(const ByteIterator &o) const noexcept { return !(*this == o); }
    Sci::Position Pos() const noexcept { return position; }
    Sci::Position PosRoundUp() const noexcept { return position; }
};

// macOS / Linux libc++ : wchar_t is 32-bit, non-BMP code points fit in one
// wchar_t. Iterator yields one wchar_t per Unicode code point.
class UTF8Iterator {
    const Document *doc;
    Sci::Position position;
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type        = wchar_t;
    using difference_type   = ptrdiff_t;
    using pointer           = wchar_t *;
    using reference         = wchar_t &;

    explicit UTF8Iterator(const Document *doc_ = nullptr, Sci::Position position_ = 0) noexcept
        : doc(doc_), position(position_) {}
    wchar_t operator*() const noexcept {
        const CharacterExtracted ce = doc->ExtractCharacter(position);
        return static_cast<wchar_t>(ce.character);
    }
    UTF8Iterator &operator++() noexcept { position = doc->NextPosition(position, 1); return *this; }
    UTF8Iterator  operator++(int) noexcept { UTF8Iterator r(*this); position = doc->NextPosition(position, 1); return r; }
    UTF8Iterator &operator--() noexcept { position = doc->NextPosition(position, -1); return *this; }
    bool operator==(const UTF8Iterator &o) const noexcept { return doc == o.doc && position == o.position; }
    bool operator!=(const UTF8Iterator &o) const noexcept { return !(*this == o); }
    Sci::Position Pos() const noexcept { return position; }
    Sci::Position PosRoundUp() const noexcept { return position; }
};

std::regex_constants::match_flag_type MatchFlags(const Document *doc, Sci::Position startPos,
                                                  Sci::Position endPos, Sci::Position lineStartPos,
                                                  Sci::Position lineEndPos) {
    std::regex_constants::match_flag_type fl = std::regex_constants::match_default;
    if (startPos != lineStartPos) {
#ifdef _LIBCPP_VERSION
        fl |= std::regex_constants::match_not_bol;
        if (!doc->IsWordStartAt(startPos)) fl |= std::regex_constants::match_not_bow;
#else
        fl |= std::regex_constants::match_prev_avail;
#endif
    }
    if (endPos != lineEndPos) {
        fl |= std::regex_constants::match_not_eol;
        if (!doc->IsWordEndAt(endPos)) fl |= std::regex_constants::match_not_eow;
    }
    return fl;
}

template<typename Iterator, typename Regex>
bool FirstMatchOnLines(const Document *doc, const Regex &regexp,
                       const RESearchRange &resr,
                       Sci::Position &posMatch, Sci::Position &lenMatch,
                       std::array<Sci::Position, 10> &bopat,
                       std::array<Sci::Position, 10> &eopat,
                       size_t &nGroups) {
    std::match_results<Iterator> match;
    bool matched = false;
    for (Sci::Line line = resr.lineRangeStart; line != resr.lineRangeBreak; line += resr.increment) {
        const Sci::Position lineStartPos = doc->LineStart(line);
        const Sci::Position lineEndPos   = doc->LineEnd(line);
        const Range lineRange = resr.LineRange(line, lineStartPos, lineEndPos);
        const Iterator itStart(doc, lineRange.start);
        const Iterator itEnd  (doc, lineRange.end);
        const auto fm = MatchFlags(doc, lineRange.start, lineRange.end, lineStartPos, lineEndPos);
        std::regex_iterator<Iterator> it(itStart, itEnd, regexp, fm);
        for (const std::regex_iterator<Iterator> last; it != last; ++it) {
            match = *it;
            matched = true;
            if (resr.increment > 0) break;   // forward: first match
            // backward: keep iterating to find the LAST one in this line
        }
        if (matched) break;
    }
    if (!matched) return false;

    nGroups = match.size();
    for (size_t co = 0; co < nGroups && co < bopat.size(); co++) {
        bopat[co] = match[co].first.Pos();
        eopat[co] = match[co].second.PosRoundUp();
    }
    posMatch = bopat[0];
    lenMatch = eopat[0] - bopat[0];
    return true;
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────
// NppRegexSearch — the RegexSearchBase override
// ────────────────────────────────────────────────────────────────────────

class NppRegexSearch : public RegexSearchBase {
public:
    explicit NppRegexSearch(CharClassify * /*charClassTable*/) {}
    ~NppRegexSearch() override = default;

    Sci::Position FindText(Document *doc, Sci::Position minPos, Sci::Position maxPos,
                          const char *s, bool caseSensitive, bool word, bool wordStart,
                          Scintilla::FindOption flags, Sci::Position *length) override;

    const char *SubstituteByPosition(Document *doc, const char *text, Sci::Position *length) override;

private:
    // ---- Tracks the most recent match so the *next* FindText call can be
    // recognized as a continuation even when the host inserted/deleted text
    // at the match position in between (the Replace-All pattern). Subscribes
    // to the document as a DocWatcher so it observes those edits directly,
    // exactly like Windows boostregex/'s Match class.
    class LastMatch final : public DocWatcher {
    public:
        ~LastMatch() override { setDocument(nullptr); }

        void clear() { set(nullptr, -1, -1); }

        void set(Document *doc, Sci::Position position, Sci::Position endPosition) {
            setDocument(doc);
            _position    = position;
            _endPosition = endPosition;
            _endPositionForContinuationCheck = endPosition;
            _documentModified = false;
        }

        bool isContinuationSearch(const Document *doc, Sci::Position startPosition, int direction) const {
            if (doc != _document || _documentModified) return false;
            if (direction > 0) return startPosition == _endPositionForContinuationCheck;
            return startPosition == _position;
        }

        bool isEmpty()   const { return _position == _endPosition; }
        Sci::Position length() const { return _endPosition - _position; }

        // ---- DocWatcher overrides (only NotifyModified / NotifyDeleted do work) ----
        void NotifyModified(Document *doc, DocModification mh, void *) override {
            if (doc != _document) return;
            using MF = Scintilla::ModificationFlags;
            if (FlagSet(mh.modificationType, MF::Undo | MF::Redo)) {
                _documentModified = true;
                return;
            }
            if (FlagSet(mh.modificationType, MF::DeleteText)) {
                if (mh.position == _position && mh.length == length()) {
                    // Host deleted exactly the text we just matched (the
                    // first half of a replace target). Slide the continuation
                    // anchor back to the match start so the subsequent
                    // InsertText restores it precisely.
                    _endPositionForContinuationCheck = _position;
                } else {
                    _documentModified = true;
                }
                return;
            }
            if (FlagSet(mh.modificationType, MF::InsertText)) {
                if (mh.position == _position && _position == _endPositionForContinuationCheck) {
                    // Insertion at our anchor (the second half of a replace).
                    // Advance the continuation anchor by the inserted length
                    // so the next FindText call from the same loop is
                    // recognized as a continuation.
                    _endPositionForContinuationCheck += mh.length;
                } else {
                    _documentModified = true;
                }
            }
        }

        void NotifyDeleted(Document *deletedDoc, void *) noexcept override {
            // Document is being torn down — drop the pointer WITHOUT calling
            // RemoveWatcher (Scintilla forbids it from inside NotifyDeleted).
            if (deletedDoc == _document) {
                _document = nullptr;
                _position = _endPosition = _endPositionForContinuationCheck = -1;
            }
        }

        // Unused virtuals.
        void NotifyModifyAttempt(Document *, void *) override {}
        void NotifySavePoint(Document *, void *, bool) override {}
        void NotifyStyleNeeded(Document *, void *, Sci::Position) override {}
        void NotifyErrorOccurred(Document *, void *, Scintilla::Status) override {}
        void NotifyGroupCompleted(Document *, void *) noexcept override {}

    private:
        void setDocument(Document *newDocument) {
            if (newDocument == _document) return;
            if (_document)    _document->RemoveWatcher(this, nullptr);
            _document = newDocument;
            if (_document)    _document->AddWatcher(this, nullptr);
        }

        Document     *_document = nullptr;
        bool          _documentModified = false;
        Sci::Position _position    = -1;
        Sci::Position _endPosition = -1;
        Sci::Position _endPositionForContinuationCheck = -1;
    };

    // Advance pos by one user-perceived character (DBCS- and CRLF-aware).
    static Sci::Position NextCharacter(const Document *doc, Sci::Position pos, bool skipCRLFAsOne) {
        if (skipCRLFAsOne && pos < doc->Length() && doc->CharAt(pos) == '\r' &&
            pos + 1 < doc->Length() && doc->CharAt(pos + 1) == '\n') {
            return pos + 2;
        }
        const Sci::Position next = doc->NextPosition(pos, 1);
        return (next > pos) ? next : pos + 1;
    }

    // Capture group positions from the last match (for SubstituteByPosition).
    std::array<Sci::Position, 10> _bopat{};
    std::array<Sci::Position, 10> _eopat{};
    size_t       _nGroups = 0;
    Document    *_lastDoc = nullptr;

    LastMatch    _lastMatch;
    std::string  _substituted;
};

// Factory called by Scintilla when SCI_OWNREGEX is defined.
namespace Scintilla::Internal {
#ifdef SCI_OWNREGEX
RegexSearchBase *CreateRegexSearch(CharClassify *charClassTable) {
    return new NppRegexSearch(charClassTable);
}
#endif
} // namespace Scintilla::Internal

// ────────────────────────────────────────────────────────────────────────
// FindText: per-line std::regex search with empty-match rejection at the
// continuation start position. Direction (forward / backward) is derived
// from the sign of (maxPos - minPos).
// ────────────────────────────────────────────────────────────────────────
Sci::Position NppRegexSearch::FindText(Document *doc, Sci::Position minPos, Sci::Position maxPos,
                                       const char *regexString, bool caseSensitive,
                                       bool /*word*/, bool /*wordStart*/,
                                       Scintilla::FindOption sciFlags, Sci::Position *lengthRet) {
    try {
        const int rawFlags = static_cast<int>(sciFlags);

        // Flag decoding. The defaults match upstream BuiltinRegex (empty
        // matches always allowed) so plugins that never set EMPTYMATCH_*
        // see no behavior change.
        const bool emptyNotAfterMatch = (rawFlags & SCFIND_REGEXP_EMPTYMATCH_NOTAFTERMATCH) != 0;
        const bool allowEmptyAtStart  = (rawFlags & SCFIND_REGEXP_EMPTYMATCH_ALLOWATSTART)  != 0;
        const bool skipCRLFAsOne      = (rawFlags & SCFIND_REGEXP_SKIPCRLFASONE)            != 0;

        const int direction = (minPos <= maxPos) ? 1 : -1;
        const Sci::Position originalStart = minPos;
        const bool isContinuation = (direction > 0) &&
                                    _lastMatch.isContinuationSearch(doc, originalStart, direction);

        // Reject empty match at startPosition only when:
        //   1. caller asked for NOTAFTERMATCH semantics, AND
        //   2. this call continues where the last match ended, AND
        //   3. caller didn't override with ALLOWATSTART.
        const bool rejectEmptyAtStart = emptyNotAfterMatch && isContinuation && !allowEmptyAtStart;

        std::regex::flag_type compileFlags = std::regex::ECMAScript;
        if (!caseSensitive) compileFlags |= std::regex::icase;

        const bool isUtf8 = (doc->CodePage() == 65001 /* SC_CP_UTF8 */);

        Sci::Position currentMin = minPos;
        const Sci::Position currentMax = maxPos;
        bool found = false;
        Sci::Position posMatch = -1;
        Sci::Position lenMatch = 0;

        for (;;) {
            if (direction > 0 && currentMin > currentMax) break;
            if (direction < 0 && currentMin < currentMax) break;

            const RESearchRange resr(doc, currentMin, currentMax);

            if (isUtf8) {
                std::wstring ws;
                const std::string s8(regexString);
                const size_t wlen = UTF16Length(s8);
                ws.resize(wlen);
                UTF16FromUTF8(s8, ws.data(), wlen);
                std::wregex regexp;
                regexp.assign(ws, compileFlags);
                found = FirstMatchOnLines<UTF8Iterator>(doc, regexp, resr,
                                                       posMatch, lenMatch,
                                                       _bopat, _eopat, _nGroups);
            } else {
                std::regex regexp;
                regexp.assign(regexString, compileFlags);
                found = FirstMatchOnLines<ByteIterator>(doc, regexp, resr,
                                                       posMatch, lenMatch,
                                                       _bopat, _eopat, _nGroups);
            }

            if (!found) break;

            // Empty-match rejection — the heart of the EMPTYMATCH_NOTAFTERMATCH
            // semantics. We only ever reject "empty at the ORIGINAL startPos",
            // not "empty at the advanced currentMin", so once we've stepped
            // past originalStart the next empty match is accepted normally.
            const bool isEmpty = (lenMatch == 0);
            const bool atOriginalStart = (direction > 0)
                                          ? (posMatch == originalStart)
                                          : (posMatch + lenMatch == originalStart);
            if (isEmpty && rejectEmptyAtStart && atOriginalStart) {
                const Sci::Position next = (direction > 0)
                                            ? NextCharacter(doc, posMatch, skipCRLFAsOne)
                                            : doc->NextPosition(posMatch, -1);
                if ((direction > 0 && next <= currentMin) ||
                    (direction < 0 && next >= currentMin)) {
                    found = false;   // no forward progress possible
                    break;
                }
                currentMin = next;
                continue;
            }

            break;   // accepted
        }

        if (found) {
            *lengthRet = lenMatch;
            _lastDoc = doc;
            _lastMatch.set(doc, posMatch, posMatch + lenMatch);
            return posMatch;
        }

        _lastMatch.clear();
        return -1;

    } catch (const std::regex_error &) {
        return -1;
    } catch (...) {
        return -1;
    }
}

// ────────────────────────────────────────────────────────────────────────
// SubstituteByPosition — expand backrefs (\0..\9) and escape sequences
// (\a \b \f \n \r \t \v \\) the same way BuiltinRegex does. Reads capture
// positions from _bopat/_eopat populated by the last FindText.
// ────────────────────────────────────────────────────────────────────────
const char *NppRegexSearch::SubstituteByPosition(Document *doc, const char *text, Sci::Position *length) {
    _substituted.clear();

    // Stale state — pass replacement through verbatim. Matches BuiltinRegex's
    // behavior when called without a prior successful match.
    if (doc != _lastDoc || _nGroups == 0) {
        _substituted.assign(text, *length);
        *length = static_cast<Sci::Position>(_substituted.size());
        return _substituted.c_str();
    }

    for (Sci::Position j = 0; j < *length; j++) {
        if (text[j] == '\\') {
            const char ch = text[++j];
            if (ch >= '0' && ch <= '9') {
                const unsigned int patNum = static_cast<unsigned int>(ch - '0');
                if (patNum < _nGroups) {
                    const Sci::Position startPos = _bopat[patNum];
                    const Sci::Position len      = _eopat[patNum] - startPos;
                    if (len > 0) {
                        const size_t before = _substituted.size();
                        _substituted.resize(before + static_cast<size_t>(len));
                        doc->GetCharRange(_substituted.data() + before, startPos, len);
                    }
                }
            } else {
                switch (ch) {
                    case 'a':  _substituted.push_back('\a'); break;
                    case 'b':  _substituted.push_back('\b'); break;
                    case 'f':  _substituted.push_back('\f'); break;
                    case 'n':  _substituted.push_back('\n'); break;
                    case 'r':  _substituted.push_back('\r'); break;
                    case 't':  _substituted.push_back('\t'); break;
                    case 'v':  _substituted.push_back('\v'); break;
                    case '\\': _substituted.push_back('\\'); break;
                    default:
                        // Unknown escape — preserve both chars literally.
                        _substituted.push_back('\\');
                        j--;
                        break;
                }
            }
        } else {
            _substituted.push_back(text[j]);
        }
    }
    *length = static_cast<Sci::Position>(_substituted.size());
    return _substituted.c_str();
}
