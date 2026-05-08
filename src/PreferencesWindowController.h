#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

// NSUserDefaults keys (exported so EditorView can read them)
extern NSString *const kPrefTabWidth;
extern NSString *const kPrefUseTabs;
extern NSString *const kPrefAutoIndent;          // NSInteger 0=None 1=Advanced 2=Basic
extern NSString *const kPrefBackspaceUnindent;   // BOOL, default NO
extern NSString *const kPrefTabOverrides;        // NSDictionary<langName, @{@"tabSize":@N, @"useTabs":@BOOL}>
extern NSString *const kPrefShowLineNumbers;
/// Word wrap. Persistent across launches (default NO).
/// Three UI surfaces all read/write this key: the Preferences > Editor
/// "Word wrap" checkbox, the View > Word Wrap menu item, and the toolbar
/// Word Wrap button. Each writes the new state to kPrefWordWrap; toolbar/
/// menu also propagates to every open editor across all open windows via
/// NPPWordWrapSessionChanged broadcast (so a click universalizes the
/// state mid-session). New tabs inherit the saved state via
/// EditorView.applyPreferencesFromDefaults reading this key on init.
/// RTL editors are skipped from the OFF path (they require wrap on for
/// layout — see EditorView.setTextDirectionRTL: + _savedWrapBeforeRTL).
extern NSString *const kPrefWordWrap;
extern NSString *const kPrefHighlightCurrentLine;
extern NSString *const kPrefEOLType;
extern NSString *const kPrefEncoding;
extern NSString *const kPrefAutoBackup;
extern NSString *const kPrefBackupInterval;
extern NSString *const kPrefZoomLevel;
extern NSString *const kPrefSpellCheck;          // BOOL, default NO
extern NSString *const kPrefAutoCompleteEnable;  // BOOL, default YES
extern NSString *const kPrefAutoCompleteMinChars;// NSInteger 1-9, default 1
extern NSString *const kPrefAutoCloseBrackets;   // BOOL, default YES
extern NSString *const kPrefShowFullPathInTitle; // BOOL, default NO
extern NSString *const kPrefCaretWidth;          // NSInteger 1-3, default 1
extern NSString *const kPrefTabMaxLabelWidth;    // NSInteger pixels, default 190
extern NSString *const kPrefTabCloseButton;      // BOOL, default YES
extern NSString *const kPrefDoubleClickTabClose; // BOOL, default NO
extern NSString *const kPrefTabBarWrap;          // BOOL, default YES
extern NSString *const kPrefVirtualSpace;        // BOOL, default NO
extern NSString *const kPrefScrollBeyondLastLine;// BOOL, default NO
extern NSString *const kPrefCaretBlinkRate;      // NSInteger ms, default 500
extern NSString *const kPrefFontQuality;         // NSInteger 0-3, default 3 (LCD)
extern NSString *const kPrefCopyLineNoSelection; // BOOL, default YES
extern NSString *const kPrefSmartHighlight;      // BOOL, default YES
extern NSString *const kPrefFillFindWithSelection;// BOOL, default YES
extern NSString *const kPrefFuncParamsHint;      // BOOL, default NO
// Tier 1 booleans
extern NSString *const kPrefShowStatusBar;       // BOOL, default YES
extern NSString *const kPrefMuteSounds;          // BOOL, default NO
extern NSString *const kPrefSaveAllConfirm;      // BOOL, default NO
extern NSString *const kPrefPluginSplitViewRouting; // BOOL, default YES — route plugin SCI messages to split view
extern NSString *const kPrefRightClickKeepsSel;  // BOOL, default NO
extern NSString *const kPrefDisableTextDragDrop; // BOOL, default NO
extern NSString *const kPrefMonoFontFind;        // BOOL, default NO
extern NSString *const kPrefConfirmReplaceAll;   // BOOL, default YES
extern NSString *const kPrefReplaceAndStop;      // BOOL, default NO
extern NSString *const kPrefSmartHiliteCase;     // BOOL, default NO
extern NSString *const kPrefSmartHiliteWord;     // BOOL, default NO
extern NSString *const kPrefDateTimeReverse;     // BOOL, default NO
extern NSString *const kPrefKeepAbsentSession;   // BOOL, default NO
extern NSString *const kPrefShowBookmarkMargin;  // BOOL, default YES
extern NSString *const kPrefShowEOL;             // BOOL, default NO
extern NSString *const kPrefShowWhitespace;      // BOOL, default NO
// Tier 2
extern NSString *const kPrefEdgeColumn;          // NSInteger, default 0 (0=off)
extern NSString *const kPrefEdgeMode;            // NSInteger, 0=off 1=line 2=background
extern NSString *const kPrefPaddingLeft;         // NSInteger, 0-30, default 0
extern NSString *const kPrefPaddingRight;        // NSInteger, 0-30, default 0
extern NSString *const kPrefPanelKeepState;      // BOOL, default YES
extern NSString *const kPrefFoldStyle;           // NSInteger, 0=box 1=circle 2=arrow 3=simple 4=none
extern NSString *const kPrefLineNumDynWidth;     // BOOL, default YES
extern NSString *const kPrefInSelThreshold;      // NSInteger, default 1024
extern NSString *const kPrefFuncListUseXML;      // BOOL, default YES — use XML parsers vs hardcoded regex
extern NSString *const kPrefToolbarIconScale;    // double, 0.50/0.75/0.90/1.00/1.25/1.50, default 1.0 — restart required

// Theme / Style Configurator keys (hex color strings "#RRGGBB")
extern NSString *const kPrefThemePreset;    // preset name or "Custom"
extern NSString *const kPrefStyleFg;        // default foreground
extern NSString *const kPrefStyleBg;        // default background
extern NSString *const kPrefStyleComment;
extern NSString *const kPrefStyleKeyword;
extern NSString *const kPrefStyleString;
extern NSString *const kPrefStyleNumber;
extern NSString *const kPrefStylePreproc;
extern NSString *const kPrefStyleFontName;  // e.g. "Menlo"
extern NSString *const kPrefStyleFontSize;  // integer stored as NSNumber

/// Modeless preferences window. Call +sharedController to get the singleton.
@interface PreferencesWindowController : NSWindowController

+ (instancetype)sharedController;

@end

NS_ASSUME_NONNULL_END
