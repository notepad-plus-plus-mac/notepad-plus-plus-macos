#import "PreferencesWindowController.h"
#import "NppLocalizer.h"
#import "NppLangsManager.h"
#import "NppThemeManager.h"
#import "StyleConfiguratorWindowController.h"

// ── NSUserDefaults keys (mirrors NPP settings) ────────────────────────────────
NSString *const kPrefTabWidth           = @"tabWidth";
NSString *const kPrefUseTabs            = @"useTabs";
NSString *const kPrefAutoIndent         = @"autoIndent";  // 0=None 1=Advanced 2=Basic
NSString *const kPrefBackspaceUnindent = @"backspaceUnindent";
NSString *const kPrefTabOverrides      = @"tabOverrides"; // {langName: {tabSize:N, useTabs:BOOL}}
NSString *const kPrefShowLineNumbers    = @"showLineNumbers";
NSString *const kPrefHighlightCurrentLine = @"highlightCurrentLine";
NSString *const kPrefEOLType            = @"eolType";       // 0=CRLF 1=LF 2=CR
NSString *const kPrefEncoding           = @"encoding";      // 0=UTF-8 1=Latin-1
NSString *const kPrefAutoBackup         = @"autoBackup";
NSString *const kPrefBackupInterval     = @"backupInterval"; // seconds
NSString *const kPrefZoomLevel          = @"zoomLevel";
NSString *const kPrefSpellCheck         = @"spellCheck";
NSString *const kPrefAutoCompleteEnable  = @"autoCompleteEnable";
NSString *const kPrefAutoCompleteMinChars = @"autoCompleteMinChars";
NSString *const kPrefAutoCloseBrackets   = @"autoCloseBrackets";
NSString *const kPrefShowFullPathInTitle = @"showFullPathInTitle";
NSString *const kPrefCaretWidth          = @"caretWidth";
NSString *const kPrefTabMaxLabelWidth    = @"tabMaxLabelWidth";
NSString *const kPrefTabCloseButton      = @"tabCloseButton";
NSString *const kPrefDoubleClickTabClose = @"doubleClickTabClose";
NSString *const kPrefVirtualSpace        = @"virtualSpace";
NSString *const kPrefScrollBeyondLastLine= @"scrollBeyondLastLine";
NSString *const kPrefCaretBlinkRate      = @"caretBlinkRate";
NSString *const kPrefFontQuality         = @"fontQuality";
NSString *const kPrefCopyLineNoSelection = @"copyLineNoSelection";
NSString *const kPrefSmartHighlight      = @"smartHighlight";
NSString *const kPrefFillFindWithSelection = @"fillFindWithSelection";
NSString *const kPrefFuncParamsHint      = @"funcParamsHint";
NSString *const kPrefShowStatusBar       = @"showStatusBar";
NSString *const kPrefMuteSounds          = @"muteSounds";
NSString *const kPrefSaveAllConfirm      = @"saveAllConfirm";
NSString *const kPrefPluginSplitViewRouting = @"pluginSplitViewRouting";
NSString *const kPrefRightClickKeepsSel  = @"rightClickKeepsSel";
NSString *const kPrefDisableTextDragDrop = @"disableTextDragDrop";
NSString *const kPrefMonoFontFind        = @"monoFontFind";
NSString *const kPrefConfirmReplaceAll   = @"confirmReplaceAll";
NSString *const kPrefReplaceAndStop      = @"replaceAndStop";
NSString *const kPrefSmartHiliteCase     = @"smartHiliteCase";
NSString *const kPrefSmartHiliteWord     = @"smartHiliteWord";
NSString *const kPrefDateTimeReverse     = @"dateTimeReverse";
NSString *const kPrefKeepAbsentSession   = @"keepAbsentSession";
NSString *const kPrefShowBookmarkMargin  = @"showBookmarkMargin";
NSString *const kPrefShowEOL             = @"showEOL";
NSString *const kPrefShowWhitespace      = @"showWhitespace";
NSString *const kPrefEdgeColumn          = @"edgeColumn";
NSString *const kPrefEdgeMode            = @"edgeMode";
NSString *const kPrefPaddingLeft         = @"paddingLeft";
NSString *const kPrefPaddingRight        = @"paddingRight";
NSString *const kPrefPanelKeepState      = @"panelKeepState";
NSString *const kPrefFoldStyle           = @"foldStyle";
NSString *const kPrefLineNumDynWidth     = @"lineNumDynWidth";
NSString *const kPrefInSelThreshold      = @"inSelThreshold";
NSString *const kPrefFuncListUseXML      = @"funcListUseXML";

// Theme / Style Configurator keys
NSString *const kPrefThemePreset        = @"themePreset";
NSString *const kPrefStyleFg            = @"styleFg";
NSString *const kPrefStyleBg            = @"styleBg";
NSString *const kPrefStyleComment       = @"styleComment";
NSString *const kPrefStyleKeyword       = @"styleKeyword";
NSString *const kPrefStyleString        = @"styleString";
NSString *const kPrefStyleNumber        = @"styleNumber";
NSString *const kPrefStylePreproc       = @"stylePreproc";
NSString *const kPrefStyleFontName      = @"styleFontName";
NSString *const kPrefStyleFontSize      = @"styleFontSize";

// Flipped NSView so scroll content starts at top-left
@interface _NPPFlippedView : NSView
@end
@implementation _NPPFlippedView
- (BOOL)isFlipped { return YES; }
@end

// ── Sidebar page definitions ─────────────────────────────────────────────────
// Each entry: @{@"title": name} or @{@"separator": @YES}
// Pages are built lazily and cached.

// ── PreferencesWindowController ───────────────────────────────────────────────

@interface PreferencesWindowController () <NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation PreferencesWindowController {
    NSTableView          *_sidebarTable;
    NSScrollView         *_contentScroll;
    NSView               *_contentArea;
    NSMutableArray       *_pageNames;     // sidebar row titles (NSString or @"-" for separator)
    NSMutableDictionary  *_pageViews;     // pageTitle → NSView (lazy cache)
    NSPopUpButton        *_languagePopup; // General page — language selector
    NSArray<NSString *>  *_indentLangNames;    // Indentation page — "[Default]" + language display names
    NSDictionary<NSString *, NSString *> *_indentDisplayToInternal; // "Python" → "python"
    NSString             *_indentSelectedLang; // currently selected internal lang name (nil = [Default])
}

+ (void)load {
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
        kPrefTabWidth:           @4,
        kPrefUseTabs:            @YES,
        kPrefAutoIndent:         @1,   // 0=None 1=Advanced 2=Basic
        kPrefBackspaceUnindent:  @NO,
        kPrefShowLineNumbers:    @YES,
        kPrefHighlightCurrentLine: @YES,
        kPrefEOLType:            @1,
        kPrefEncoding:           @0,
        kPrefAutoBackup:         @YES,
        kPrefBackupInterval:     @60,
        kPrefZoomLevel:          @0,
        kPrefLanguage:           @"english",
        // Default (light) theme colors
        kPrefThemePreset:        @"Default",
        kPrefStyleFg:            @"#000000",
        kPrefStyleBg:            @"#FFFFFF",
        kPrefStyleComment:       @"#008000",
        kPrefStyleKeyword:       @"#0000FF",
        kPrefStyleString:        @"#A31515",
        kPrefStyleNumber:        @"#098658",
        kPrefStylePreproc:       @"#800080",
        kPrefStyleFontName:      @"Menlo",
        kPrefStyleFontSize:      @11,
        kPrefAutoCompleteEnable:   @YES,
        kPrefAutoCompleteMinChars: @1,
        kPrefAutoCloseBrackets:    @YES,
        kPrefShowFullPathInTitle:  @NO,
        kPrefCaretWidth:           @1,
        kPrefTabMaxLabelWidth:     @190,
        kPrefTabCloseButton:       @YES,
        kPrefDoubleClickTabClose:  @NO,
        kPrefVirtualSpace:         @NO,
        kPrefScrollBeyondLastLine: @NO,
        kPrefCaretBlinkRate:       @500,
        kPrefFontQuality:          @3,   // 0=default 1=none 2=antialiased 3=LCD
        kPrefCopyLineNoSelection:  @YES,
        kPrefSmartHighlight:       @YES,
        kPrefFillFindWithSelection:@YES,
        kPrefFuncParamsHint:       @NO,
        kPrefShowStatusBar:        @YES,
        kPrefMuteSounds:           @NO,
        kPrefSaveAllConfirm:       @NO,
        kPrefPluginSplitViewRouting: @YES,
        kPrefRightClickKeepsSel:   @NO,
        kPrefDisableTextDragDrop:  @NO,
        kPrefMonoFontFind:         @NO,
        kPrefConfirmReplaceAll:    @YES,
        kPrefReplaceAndStop:       @NO,
        kPrefSmartHiliteCase:      @NO,
        kPrefSmartHiliteWord:      @NO,
        kPrefDateTimeReverse:      @NO,
        kPrefKeepAbsentSession:    @NO,
        kPrefShowBookmarkMargin:   @YES,
        kPrefShowEOL:              @NO,
        kPrefShowWhitespace:       @NO,
        kPrefEdgeColumn:           @0,
        kPrefEdgeMode:             @0,    // 0=off 1=line 2=background
        kPrefPaddingLeft:          @0,
        kPrefPaddingRight:         @0,
        kPrefPanelKeepState:       @YES,
        kPrefFoldStyle:            @0,    // 0=box 1=circle 2=arrow 3=simple 4=none
        kPrefLineNumDynWidth:      @YES,
        kPrefInSelThreshold:       @1024,
        kPrefFuncListUseXML:       @YES,
        kPrefDarkMode:             @0,   // 0=Auto, 1=Light, 2=Dark
        kPrefToolbarIconStyle:     @0,   // 0=Fluent, 1=Classic
    }];
    // Force-upgrade any stale @NO value stored by earlier builds.
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    if (![ud objectForKey:kPrefUseTabs]) {
    } else if ([ud objectForKey:@"_useTabsDefaultApplied"] == nil) {
        [ud setBool:YES forKey:kPrefUseTabs];
        [ud setBool:YES forKey:@"_useTabsDefaultApplied"];
    }
}

+ (instancetype)sharedController {
    static PreferencesWindowController *shared;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        shared = [[self alloc] init];
    });
    return shared;
}

- (instancetype)init {
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 700, 480)
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    win.title = @"Preferences";
    [win center];
    self = [super initWithWindow:win];
    if (self) {
        [self registerDefaults];
        [self _buildSidebarLayout];
        [self retranslateUI];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(_locChanged:)
                                                     name:NPPLocalizationChanged object:nil];
    }
    return self;
}

- (void)_locChanged:(NSNotification *)n {
    [self retranslateUI];
    [self _rebuildLanguagePopup];
}

- (void)retranslateUI {
    NppLocalizer *loc = [NppLocalizer shared];
    self.window.title = [loc translate:@"Preferences"];

    // Rebuild sidebar page names with new translations
    _pageNames = [NSMutableArray arrayWithArray:@[
        [loc translate:@"General"],
        [loc translate:@"Editor"],
        [loc translate:@"Indentation"],
        [loc translate:@"Tab Bar"],
        [loc translate:@"Dark Mode"],
        [loc translate:@"Toolbar"],
        [loc translate:@"Margins"],
        [loc translate:@"New Document"],
        [loc translate:@"Backup"],
        [loc translate:@"Auto-Completion"],
        [loc translate:@"Searching"],
        [loc translate:@"MISC."],
    ]];
    // Invalidate cached page views so they rebuild with new translations
    [_pageViews removeAllObjects];
    [_sidebarTable reloadData];

    // Re-show current page
    NSInteger row = _sidebarTable.selectedRow;
    if (row >= 0) [self _showPageAtIndex:row];
}

- (void)registerDefaults {
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
        kPrefTabWidth:           @4,
        kPrefUseTabs:            @YES,
        kPrefAutoIndent:         @1,   // 0=None 1=Advanced 2=Basic
        kPrefBackspaceUnindent:  @NO,
        kPrefShowLineNumbers:    @YES,
        kPrefHighlightCurrentLine: @YES,
        kPrefEOLType:            @1,
        kPrefEncoding:           @0,
        kPrefAutoBackup:         @YES,
        kPrefBackupInterval:     @60,
    }];
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sidebar Layout
// ═══════════════════════════════════════════════════════════════════════════════

- (void)_buildSidebarLayout {
    NSView *root = self.window.contentView;

    // ── Page names (sidebar rows) ────────────────────────────────────────────
    _pageNames = [NSMutableArray arrayWithArray:@[
        [[NppLocalizer shared] translate:@"General"],
        [[NppLocalizer shared] translate:@"Editor"],
        [[NppLocalizer shared] translate:@"Indentation"],
        [[NppLocalizer shared] translate:@"Tab Bar"],
        [[NppLocalizer shared] translate:@"Dark Mode"],
        [[NppLocalizer shared] translate:@"Toolbar"],
        [[NppLocalizer shared] translate:@"Margins"],
        [[NppLocalizer shared] translate:@"New Document"],
        [[NppLocalizer shared] translate:@"Backup"],
        [[NppLocalizer shared] translate:@"Auto-Completion"],
        [[NppLocalizer shared] translate:@"Searching"],
        [[NppLocalizer shared] translate:@"MISC."],
    // Future pages can be added here
    // @"Performance",
    // @"Delimiter",
    ]];
    _pageViews = [NSMutableDictionary dictionary];

    // ── Sidebar (source list table view) ─────────────────────────────────────
    NSScrollView *sidebarScroll = [[NSScrollView alloc] init];
    sidebarScroll.translatesAutoresizingMaskIntoConstraints = NO;
    sidebarScroll.hasVerticalScroller   = NO;
    sidebarScroll.hasHorizontalScroller = NO;
    sidebarScroll.drawsBackground = NO;

    _sidebarTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    _sidebarTable.headerView = nil;
    _sidebarTable.rowHeight = 24;
    _sidebarTable.intercellSpacing = NSMakeSize(0, 2);
    _sidebarTable.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    _sidebarTable.backgroundColor = [NSColor clearColor];
    _sidebarTable.dataSource = self;
    _sidebarTable.delegate   = self;

    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    col.editable = NO;
    [_sidebarTable addTableColumn:col];
    sidebarScroll.documentView = _sidebarTable;

    // ── Content area (wrapped in scroll view for long pages) ────────────────
    _contentArea = [[NSView alloc] init];
    _contentArea.translatesAutoresizingMaskIntoConstraints = NO;

    _contentScroll = [[NSScrollView alloc] init];
    _contentScroll.translatesAutoresizingMaskIntoConstraints = NO;
    _contentScroll.hasVerticalScroller   = YES;
    _contentScroll.hasHorizontalScroller = NO;
    _contentScroll.drawsBackground       = NO;
    _contentScroll.automaticallyAdjustsContentInsets = NO;
    _contentScroll.scrollerStyle = NSScrollerStyleOverlay; // macOS overlay scrollbar

    // Document view for scroll content (regular coordinate system — page views use frame positioning)
    _contentArea = [[NSView alloc] init];
    _contentScroll.documentView = _contentArea;

    // ── Close button ─────────────────────────────────────────────────────────
    NSButton *closeBtn = [NSButton buttonWithTitle:[[NppLocalizer shared] translate:@"Close"]
                                            target:self action:@selector(closePrefs:)];
    closeBtn.translatesAutoresizingMaskIntoConstraints = NO;
    closeBtn.keyEquivalent = @"\033";

    // ── Separator between sidebar and content ────────────────────────────────
    NSBox *sep = [[NSBox alloc] init];
    sep.boxType = NSBoxSeparator;
    sep.translatesAutoresizingMaskIntoConstraints = NO;

    [root addSubview:sidebarScroll];
    [root addSubview:sep];
    [root addSubview:_contentScroll];
    [root addSubview:closeBtn];

    [NSLayoutConstraint activateConstraints:@[
        // Sidebar
        [sidebarScroll.topAnchor      constraintEqualToAnchor:root.topAnchor constant:12],
        [sidebarScroll.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor constant:12],
        [sidebarScroll.widthAnchor    constraintEqualToConstant:170],
        [sidebarScroll.bottomAnchor   constraintEqualToAnchor:closeBtn.topAnchor constant:-12],

        // Separator
        [sep.topAnchor      constraintEqualToAnchor:root.topAnchor constant:8],
        [sep.leadingAnchor  constraintEqualToAnchor:sidebarScroll.trailingAnchor constant:8],
        [sep.widthAnchor    constraintEqualToConstant:1],
        [sep.bottomAnchor   constraintEqualToAnchor:closeBtn.topAnchor constant:-8],

        // Content scroll view
        [_contentScroll.topAnchor      constraintEqualToAnchor:root.topAnchor constant:12],
        [_contentScroll.leadingAnchor  constraintEqualToAnchor:sep.trailingAnchor constant:12],
        [_contentScroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-12],
        [_contentScroll.bottomAnchor   constraintEqualToAnchor:closeBtn.topAnchor constant:-12],

        // Close button
        [closeBtn.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16],
        [closeBtn.bottomAnchor   constraintEqualToAnchor:root.bottomAnchor constant:-12],
        [closeBtn.widthAnchor    constraintEqualToConstant:80],
    ]];

    // Select first real page — defer until after layout so contentSize is valid
    [_sidebarTable selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    dispatch_async(dispatch_get_main_queue(), ^{
        [self _showPageAtIndex:0];
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sidebar Data Source & Delegate
// ═══════════════════════════════════════════════════════════════════════════════

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv {
    if (tv.tag == 1400) return (NSInteger)_indentLangNames.count;
    return (NSInteger)_pageNames.count;
}

- (NSView *)tableView:(NSTableView *)tv viewForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    // Indentation language list
    if (tv.tag == 1400) {
        NSTextField *tf = [tv makeViewWithIdentifier:@"langCell" owner:nil];
        if (!tf) {
            tf = [NSTextField labelWithString:@""];
            tf.identifier = @"langCell";
            tf.font = [NSFont systemFontOfSize:12];
            tf.bordered = NO;
            tf.editable = NO;
            tf.drawsBackground = NO;
        }
        tf.stringValue = _indentLangNames[row];
        if (row == 0) tf.font = [NSFont boldSystemFontOfSize:12]; // [Default] bold
        else tf.font = [NSFont systemFontOfSize:12];
        return tf;
    }

    // Sidebar
    NSString *name = _pageNames[row];

    if ([name isEqualToString:@"-"]) {
        // Separator row
        NSBox *sep = [[NSBox alloc] init];
        sep.boxType = NSBoxSeparator;
        sep.frame = NSMakeRect(8, 10, 150, 1);
        NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 170, 20)];
        [container addSubview:sep];
        return container;
    }

    NSTextField *tf = [tv makeViewWithIdentifier:@"cell" owner:nil];
    if (!tf) {
        tf = [NSTextField labelWithString:@""];
        tf.identifier = @"cell";
        tf.font = [NSFont systemFontOfSize:13];
    }
    tf.stringValue = name;
    return tf;
}

- (CGFloat)tableView:(NSTableView *)tv heightOfRow:(NSInteger)row {
    if (tv.tag == 1400) return 18;
    NSString *name = _pageNames[row];
    return [name isEqualToString:@"-"] ? 12 : 26;
}

- (BOOL)tableView:(NSTableView *)tv shouldSelectRow:(NSInteger)row {
    if (tv.tag == 1400) return YES;
    return ![_pageNames[row] isEqualToString:@"-"]; // separators not selectable
}

- (void)tableViewSelectionDidChange:(NSNotification *)n {
    NSTableView *tv = n.object;
    if (tv.tag == 1400) {
        [self _indentLangSelectionChanged:tv];
        return;
    }
    NSInteger row = _sidebarTable.selectedRow;
    if (row >= 0) [self _showPageAtIndex:row];
}

// ═══════════════════════════════════════════════════════════════════════════════
// Page Switching
// ═══════════════════════════════════════════════════════════════════════════════

- (void)_showPageAtIndex:(NSInteger)index {
    NSString *name = _pageNames[index];
    if ([name isEqualToString:@"-"]) return;

    // Remove current content
    for (NSView *sub in [_contentArea.subviews copy])
        [sub removeFromSuperview];

    // Build or retrieve cached page view
    NSView *pageView = _pageViews[name];
    if (!pageView) {
        pageView = [self _buildPageForName:name];
        if (pageView) _pageViews[name] = pageView;
    }
    if (!pageView) return;

    // Page views use frame-based positioning: y starts high (e.g. 380) and decreases.
    // Find the bounding box of all subviews to determine actual content extent.
    CGFloat contentW = _contentScroll.contentSize.width;
    CGFloat visibleH = _contentScroll.contentSize.height;

    CGFloat maxY = 0, minY = CGFLOAT_MAX;
    for (NSView *sub in pageView.subviews) {
        CGFloat top = NSMaxY(sub.frame);
        CGFloat bot = NSMinY(sub.frame);
        if (top > maxY) maxY = top;
        if (bot < minY) minY = bot;
    }
    if (minY == CGFLOAT_MAX) { minY = 0; maxY = visibleH; }

    // Content height = span of controls + padding at top and bottom
    CGFloat controlsHeight = (maxY - minY) + 20; // 20px padding
    CGFloat pageHeight = MAX(visibleH, controlsHeight);

    // Shift controls so the topmost control sits 10px from the top of the page view
    CGFloat shiftY = pageHeight - maxY - 10;
    if (fabs(shiftY) > 1) {
        for (NSView *sub in pageView.subviews) {
            NSRect f = sub.frame;
            f.origin.y += shiftY;
            sub.frame = f;
        }
    }

    pageView.frame = NSMakeRect(0, 0, contentW, pageHeight);
    pageView.autoresizingMask = NSViewWidthSizable;
    [_contentArea addSubview:pageView];

    _contentArea.frame = NSMakeRect(0, 0, contentW, pageHeight);

    // Scroll to top (in non-flipped view, top = highest Y)
    [_contentArea scrollPoint:NSMakePoint(0, pageHeight)];
}

- (NSView *)_buildPageForName:(NSString *)name {
    NppLocalizer *loc = [NppLocalizer shared];
    if ([name isEqualToString:[loc translate:@"General"]])         return [self _buildGeneralPage];
    if ([name isEqualToString:[loc translate:@"Editor"]])          return [self _buildEditorPage];
    if ([name isEqualToString:[loc translate:@"Indentation"]])    return [self _buildIndentationPage];
    if ([name isEqualToString:[loc translate:@"Tab Bar"]])         return [self _buildTabBarPage];
    if ([name isEqualToString:[loc translate:@"Dark Mode"]])       return [self _buildDarkModePage];
    if ([name isEqualToString:[loc translate:@"Toolbar"]])        return [self _buildToolbarPage];
    if ([name isEqualToString:[loc translate:@"Margins"]])          return [self _buildMarginsPage];
    if ([name isEqualToString:[loc translate:@"New Document"]])     return [self _buildNewDocPage];
    if ([name isEqualToString:[loc translate:@"Backup"]])           return [self _buildBackupPage];
    if ([name isEqualToString:[loc translate:@"Auto-Completion"]])  return [self _buildAutoCompletionPage];
    if ([name isEqualToString:[loc translate:@"Searching"]])        return [self _buildSearchingPage];
    if ([name isEqualToString:[loc translate:@"MISC."]])            return [self _buildMiscPage];
    return nil;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Page Builders — Each returns an NSView with all controls
// ═══════════════════════════════════════════════════════════════════════════════

#pragma mark - General Page

- (NSView *)_buildGeneralPage {
    NSView *v = [[NSView alloc] init];
    CGFloat y = 380;

    // ── Localization ──
    NSTextField *sectionLabel = [NSTextField labelWithString:[[NppLocalizer shared] translate:@"Localization"]];
    sectionLabel.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    sectionLabel.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:sectionLabel];
    y -= 30;

    NSTextField *langLabel = [NSTextField labelWithString:[[NppLocalizer shared] translate:@"Language:"]];
    langLabel.frame = NSMakeRect(20, y, 90, 20);
    [v addSubview:langLabel];

    _languagePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(120, y - 2, 250, 26) pullsDown:NO];
    _languagePopup.tag = 400;
    _languagePopup.target = self;
    _languagePopup.action = @selector(prefChanged:);
    [self _rebuildLanguagePopup];
    [v addSubview:_languagePopup];
    y -= 36;

    NSTextField *hint = [NSTextField wrappingLabelWithString:
        [NSString stringWithFormat:@"%@\n%@",
         [[NppLocalizer shared] translate:@"Additional language files (.xml) can be placed in:"],
         @"~/Library/Application Support/Notepad++/localization/"]];
    hint.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    hint.textColor = NSColor.secondaryLabelColor;
    hint.frame = NSMakeRect(20, y - 16, 400, 44);
    [v addSubview:hint];
    y -= 70;

    // ── Title Bar ──
    NSTextField *tbSection = [NSTextField labelWithString:[[NppLocalizer shared] translate:@"Title Bar"]];
    tbSection.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    tbSection.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:tbSection];
    y -= 28;

    NSButton *fullPath = [NSButton checkboxWithTitle:[[NppLocalizer shared] translate:@"Show full file path in title bar"]
                                              target:self action:@selector(prefChanged:)];
    fullPath.frame = NSMakeRect(20, y, 350, 20);
    fullPath.state = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefShowFullPathInTitle]
                     ? NSControlStateValueOn : NSControlStateValueOff;
    fullPath.tag = 900;
    [v addSubview:fullPath];
    y -= 32;

    // ── Status Bar ──
    NSTextField *sbSection = [NSTextField labelWithString:[[NppLocalizer shared] translate:@"Status Bar"]];
    sbSection.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    sbSection.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:sbSection];
    y -= 28;

    NSButton *showSB = [NSButton checkboxWithTitle:[[NppLocalizer shared] translate:@"Show status bar"]
                                            target:self action:@selector(prefChanged:)];
    showSB.frame = NSMakeRect(20, y, 350, 20);
    showSB.state = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefShowStatusBar]
                   ? NSControlStateValueOn : NSControlStateValueOff;
    showSB.tag = 901;
    [v addSubview:showSB];

    return v;
}

#pragma mark - Editor Page

- (NSView *)_buildEditorPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    CGFloat y = 380;

    NppLocalizer *loc = [NppLocalizer shared];
    NSArray *checks = @[
        @[[loc translate:@"Show line numbers"],               @103, kPrefShowLineNumbers],
        @[[loc translate:@"Highlight current line"],          @105, kPrefHighlightCurrentLine],
        @[[loc translate:@"Auto-close brackets ( ) [ ] { }"], @700, kPrefAutoCloseBrackets],
        @[[loc translate:@"Enable virtual space"],            @702, kPrefVirtualSpace],
        @[[loc translate:@"Scroll beyond last line"],         @703, kPrefScrollBeyondLastLine],
        @[[loc translate:@"Copy/cut line without selection"],  @706, kPrefCopyLineNoSelection],
        @[[loc translate:@"Right-click keeps selection"],      @707, kPrefRightClickKeepsSel],
        @[[loc translate:@"Disable selected text drag-drop"],  @708, kPrefDisableTextDragDrop],
        @[[loc translate:@"Show bookmark margin"],             @709, kPrefShowBookmarkMargin],
        @[[loc translate:@"Show EOL markers"],                 @710, kPrefShowEOL],
        @[[loc translate:@"Show whitespace"],                  @711, kPrefShowWhitespace],
    ];
    for (NSArray *def in checks) {
        NSButton *chk = [NSButton checkboxWithTitle:def[0] target:self action:@selector(prefChanged:)];
        chk.frame = NSMakeRect(20, y, 350, 20);
        chk.state = [ud boolForKey:def[2]] ? NSControlStateValueOn : NSControlStateValueOff;
        chk.tag = [def[1] integerValue];
        [v addSubview:chk];
        y -= 28;
    }

    y -= 8;
    // Caret width
    NSTextField *cwLabel = [NSTextField labelWithString:[loc translate:@"Caret width:"]];
    cwLabel.frame = NSMakeRect(20, y, 100, 20);
    [v addSubview:cwLabel];
    NSPopUpButton *cwPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(130, y-2, 120, 26) pullsDown:NO];
    [cwPopup addItemsWithTitles:@[[loc translate:@"Thin (1px)"], [loc translate:@"Medium (2px)"], [loc translate:@"Thick (3px)"]]];
    [cwPopup selectItemAtIndex:[ud integerForKey:kPrefCaretWidth] - 1];
    cwPopup.tag = 701; cwPopup.target = self; cwPopup.action = @selector(prefChanged:);
    [v addSubview:cwPopup];
    y -= 32;

    // Caret blink rate
    NSTextField *brLabel = [NSTextField labelWithString:[loc translate:@"Caret blink rate (ms):"]];
    brLabel.frame = NSMakeRect(20, y, 160, 20);
    [v addSubview:brLabel];
    NSTextField *brField = [[NSTextField alloc] initWithFrame:NSMakeRect(190, y-2, 60, 22)];
    brField.integerValue = [ud integerForKey:kPrefCaretBlinkRate];
    brField.tag = 704; brField.target = self; brField.action = @selector(prefChanged:);
    [v addSubview:brField];
    y -= 32;

    // Font quality
    NSTextField *fqLabel = [NSTextField labelWithString:[loc translate:@"Font rendering:"]];
    fqLabel.frame = NSMakeRect(20, y, 120, 20);
    [v addSubview:fqLabel];
    NSPopUpButton *fqPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(150, y-2, 180, 26) pullsDown:NO];
    [fqPopup addItemsWithTitles:@[[loc translate:@"Default"], [loc translate:@"None"], [loc translate:@"Antialiased"], [loc translate:@"LCD Optimized"]]];
    [fqPopup selectItemAtIndex:[ud integerForKey:kPrefFontQuality]];
    fqPopup.tag = 705; fqPopup.target = self; fqPopup.action = @selector(prefChanged:);
    [v addSubview:fqPopup];

    return v;
}

#pragma mark - Indentation Page

// Internal language name → display name for Indentation page
static NSDictionary<NSString *, NSString *> *_langDisplayNames() {
    static NSDictionary *m;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        m = @{
            @"ada":@"Ada", @"asm":@"Assembly", @"asp":@"ASP",
            @"bash":@"Bash", @"batch":@"Batch",
            @"c":@"C", @"cs":@"C#", @"cpp":@"C++", @"cmake":@"CMake",
            @"cobol":@"COBOL", @"css":@"CSS",
            @"d":@"D", @"diff":@"Diff",
            @"erlang":@"Erlang",
            @"fortran":@"Fortran",
            @"go":@"Go", @"groovy":@"Groovy",
            @"haskell":@"Haskell", @"html":@"HTML",
            @"ini":@"INI", @"inno":@"Inno Setup",
            @"java":@"Java", @"javascript":@"JavaScript", @"json":@"JSON", @"json5":@"JSON5", @"jsp":@"JSP",
            @"latex":@"LaTeX", @"lisp":@"Lisp", @"lua":@"Lua",
            @"makefile":@"Makefile", @"matlab":@"MATLAB", @"mssql":@"MS SQL",
            @"nim":@"Nim", @"nsis":@"NSIS",
            @"objc":@"Objective-C",
            @"pascal":@"Pascal", @"perl":@"Perl", @"php":@"PHP",
            @"powershell":@"PowerShell", @"props":@"Properties", @"python":@"Python",
            @"r":@"R", @"rc":@"Resource", @"ruby":@"Ruby", @"rust":@"Rust",
            @"scheme":@"Scheme", @"smalltalk":@"Smalltalk", @"sql":@"SQL",
            @"swift":@"Swift",
            @"tcl":@"Tcl", @"tex":@"TeX", @"toml":@"TOML", @"typescript":@"TypeScript",
            @"vb":@"Visual Basic", @"vhdl":@"VHDL",
            @"xml":@"XML",
            @"yaml":@"YAML",
        };
    });
    return m;
}

- (NSView *)_buildIndentationPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    NppLocalizer *loc = [NppLocalizer shared];
    CGFloat y = 380;

    // ── Build language list: [Default] + sorted display names ──
    NSDictionary *displayMap = _langDisplayNames();
    NSArray<NSString *> *rawNames = [[NppLangsManager shared] allLanguageNames];
    NSMutableArray<NSString *> *displayNames = [NSMutableArray arrayWithObject:@"[Default]"];
    [displayNames addObject:@"Normal text"];
    NSMutableDictionary *reverseMap = [NSMutableDictionary dictionary];
    NSMutableArray *sorted = [NSMutableArray array];
    for (NSString *raw in rawNames) {
        NSString *display = displayMap[raw] ?: raw;
        [sorted addObject:display];
        reverseMap[display] = raw;
    }
    [sorted sortUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
    [displayNames addObjectsFromArray:sorted];
    _indentLangNames = [displayNames copy];
    _indentDisplayToInternal = [reverseMap copy];
    _indentSelectedLang = nil; // [Default] selected initially

    // ── Indent Settings group ──
    NSBox *indentBox = [[NSBox alloc] initWithFrame:NSMakeRect(16, y - 180, 175, 190)];
    indentBox.title = [loc translate:@"Indent Settings"];
    indentBox.titlePosition = NSAtTop;

    NSScrollView *langScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(-1, -1, 159, 165)];
    langScroll.hasVerticalScroller = YES;
    langScroll.borderType = NSBezelBorder;
    NSTableView *langTable = [[NSTableView alloc] initWithFrame:langScroll.bounds];
    NSTableColumn *langCol = [[NSTableColumn alloc] initWithIdentifier:@"lang"];
    langCol.title = @"";
    langCol.width = 139;
    [langTable addTableColumn:langCol];
    langTable.headerView = nil;
    langTable.rowHeight = 18;
    langTable.tag = 1400;
    langTable.dataSource = self;
    langTable.delegate = self;
    langScroll.documentView = langTable;
    [indentBox addSubview:langScroll];
    // Select [Default] row
    [langTable selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    [v addSubview:indentBox];

    // ── Indent options box (below Indent Settings) ──
    CGFloat optBoxTop = y - 190;  // just below indentBox
    NSBox *optBox = [[NSBox alloc] initWithFrame:NSMakeRect(19, optBoxTop - 193, 430, 193)];
    optBox.titlePosition = NSNoTitle;

    // "Use default value" checkbox at top
    NSButton *defaultChk = [NSButton checkboxWithTitle:[loc translate:@"Use default value"]
                                                target:self action:@selector(_useDefaultValueChanged:)];
    defaultChk.frame = NSMakeRect(15, 153, 200, 20);
    defaultChk.state = NSControlStateValueOn; // default is checked (use global settings)
    defaultChk.tag = 1320;
    [optBox addSubview:defaultChk];

    CGFloat oy = 118;

    NSFont *smallFont = [NSFont systemFontOfSize:[NSFont systemFontSize] - 1];

    NSTextField *sizeLabel = [NSTextField labelWithString:[loc translate:@"Indent size:"]];
    sizeLabel.frame = NSMakeRect(15, oy, 90, 20);
    sizeLabel.font = smallFont;
    sizeLabel.tag = 1330;
    [optBox addSubview:sizeLabel];

    NSTextField *sizeField = [[NSTextField alloc] initWithFrame:NSMakeRect(115, oy - 2, 50, 22)];
    sizeField.integerValue = [ud integerForKey:kPrefTabWidth];
    sizeField.tag = 100;
    sizeField.target = self;
    sizeField.action = @selector(prefChanged:);
    sizeField.enabled = NO;
    [optBox addSubview:sizeField];
    oy -= 30;

    NSTextField *usingLabel = [NSTextField labelWithString:[loc translate:@"Indent using:"]];
    usingLabel.frame = NSMakeRect(15, oy, 100, 20);
    usingLabel.font = smallFont;
    usingLabel.tag = 1331;
    [optBox addSubview:usingLabel];
    oy -= 24;

    BOOL useTabs = [ud boolForKey:kPrefUseTabs];
    NSButton *tabRadio = [NSButton radioButtonWithTitle:[loc translate:@"Tab character"]
                                                 target:self action:@selector(_indentUsingChanged:)];
    tabRadio.frame = NSMakeRect(25, oy, 150, 20);
    tabRadio.font = smallFont;
    tabRadio.tag = 1301;
    tabRadio.state = useTabs ? NSControlStateValueOn : NSControlStateValueOff;
    tabRadio.enabled = NO;
    [optBox addSubview:tabRadio];
    oy -= 24;

    NSButton *spaceRadio = [NSButton radioButtonWithTitle:[loc translate:@"Space character(s)"]
                                                   target:self action:@selector(_indentUsingChanged:)];
    spaceRadio.frame = NSMakeRect(25, oy, 150, 20);
    spaceRadio.font = smallFont;
    spaceRadio.tag = 1302;
    spaceRadio.state = useTabs ? NSControlStateValueOff : NSControlStateValueOn;
    spaceRadio.enabled = NO;
    [optBox addSubview:spaceRadio];
    oy -= 30;

    NSButton *bsChk = [NSButton checkboxWithTitle:[loc translate:@"Backspace key unindents instead of removing single space"]
                                           target:self action:@selector(prefChanged:)];
    bsChk.frame = NSMakeRect(15, oy, 400, 20);
    bsChk.font = smallFont;
    bsChk.state = [ud boolForKey:kPrefBackspaceUnindent] ? NSControlStateValueOn : NSControlStateValueOff;
    bsChk.tag = 1303;
    bsChk.enabled = NO;
    [optBox addSubview:bsChk];

    [v addSubview:optBox];

    // ── Auto-indent radio group (right of Indent Settings) ──
    NSBox *autoBox = [[NSBox alloc] initWithFrame:NSMakeRect(215, y - 90, 160, 100)];
    autoBox.title = [loc translate:@"Auto-indent"];
    autoBox.titlePosition = NSAtTop;

    NSInteger mode = [ud integerForKey:kPrefAutoIndent];
    // Migrate legacy BOOL: YES(1) → Advanced(1), NO(0) → None(0) — already compatible

    NSButton *noneRadio = [NSButton radioButtonWithTitle:[loc translate:@"None"]
                                                  target:self action:@selector(_autoIndentChanged:)];
    noneRadio.frame = NSMakeRect(15, 50, 120, 20);
    noneRadio.tag = 1310;
    noneRadio.state = (mode == 0) ? NSControlStateValueOn : NSControlStateValueOff;
    [autoBox addSubview:noneRadio];

    NSButton *basicRadio = [NSButton radioButtonWithTitle:[loc translate:@"Basic"]
                                                   target:self action:@selector(_autoIndentChanged:)];
    basicRadio.frame = NSMakeRect(15, 30, 120, 20);
    basicRadio.tag = 1311;
    basicRadio.state = (mode == 2) ? NSControlStateValueOn : NSControlStateValueOff;
    [autoBox addSubview:basicRadio];

    NSButton *advRadio = [NSButton radioButtonWithTitle:[loc translate:@"Advanced"]
                                                 target:self action:@selector(_autoIndentChanged:)];
    advRadio.frame = NSMakeRect(15, 10, 120, 20);
    advRadio.tag = 1312;
    advRadio.state = (mode == 1) ? NSControlStateValueOn : NSControlStateValueOff;
    [autoBox addSubview:advRadio];

    [v addSubview:autoBox];

    return v;
}

// Resolve indent settings for the selected language.
// Returns {tabSize, useTabs} from: user override → langs.xml tabSettings → global default.
- (void)_getIndentForLang:(NSString *)lang tabSize:(NSInteger *)outSize useTabs:(BOOL *)outUseTabs hasOverride:(BOOL *)outHasOverride {
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    NSInteger globalSize = [ud integerForKey:kPrefTabWidth];
    if (globalSize < 1) globalSize = 4;
    BOOL globalUseTabs = [ud boolForKey:kPrefUseTabs];

    if (!lang.length) {
        // [Default] or Normal text — show global settings
        *outSize = globalSize;
        *outUseTabs = globalUseTabs;
        *outHasOverride = NO;
        return;
    }

    // Check user override first
    NSDictionary *overrides = [ud dictionaryForKey:kPrefTabOverrides];
    NSDictionary *langOverride = overrides[lang];
    if (langOverride) {
        *outSize = [langOverride[@"tabSize"] integerValue];
        if (*outSize < 1) *outSize = 4;
        *outUseTabs = [langOverride[@"useTabs"] boolValue];
        *outHasOverride = YES;
        return;
    }

    // Check langs.xml built-in tabSettings
    NppLangDef *def = [[NppLangsManager shared] langDefForName:lang];
    if (def && def.tabSettings >= 0) {
        NSInteger ts = def.tabSettings;
        NSInteger xmlSize = ts & 0x7F;
        BOOL xmlUseSpaces = (ts & 0x80) != 0;
        if (xmlSize > 0) {
            *outSize = xmlSize;
            *outUseTabs = !xmlUseSpaces;
            *outHasOverride = NO; // built-in, not user override
            return;
        }
    }

    // Fall back to global
    *outSize = globalSize;
    *outUseTabs = globalUseTabs;
    *outHasOverride = NO;
}

- (void)_indentLangSelectionChanged:(NSTableView *)tv {
    NSInteger row = tv.selectedRow;
    if (row < 0) return;

    NSString *displayName = _indentLangNames[row];
    // Map display name to internal name
    if (row == 0) { // [Default]
        _indentSelectedLang = nil;
    } else if (row == 1) { // Normal text
        _indentSelectedLang = @"";
    } else {
        _indentSelectedLang = _indentDisplayToInternal[displayName] ?: displayName.lowercaseString;
    }

    // Load settings for this language into the controls
    NSInteger tabSize = 4;
    BOOL useTabs = YES;
    BOOL hasOverride = NO;
    [self _getIndentForLang:_indentSelectedLang tabSize:&tabSize useTabs:&useTabs hasOverride:&hasOverride];

    // Find controls in the optBox (tag-based lookup)
    NSView *optBox = [tv.window.contentView viewWithTag:1320].superview;
    if (!optBox) return;

    for (NSView *sub in optBox.subviews) {
        if (sub.tag == 100 && [sub isKindOfClass:[NSTextField class]])
            [(NSTextField *)sub setIntegerValue:tabSize];
        else if (sub.tag == 1301 && [sub isKindOfClass:[NSButton class]])
            [(NSButton *)sub setState:useTabs ? NSControlStateValueOn : NSControlStateValueOff];
        else if (sub.tag == 1302 && [sub isKindOfClass:[NSButton class]])
            [(NSButton *)sub setState:useTabs ? NSControlStateValueOff : NSControlStateValueOn];
        else if (sub.tag == 1320 && [sub isKindOfClass:[NSButton class]]) {
            BOOL isDefault = (_indentSelectedLang == nil); // [Default] row
            if (isDefault) {
                // [Default] row: checkbox hidden, controls always enabled
                [(NSButton *)sub setHidden:YES];
            } else {
                [(NSButton *)sub setHidden:NO];
                [(NSButton *)sub setState:hasOverride ? NSControlStateValueOff : NSControlStateValueOn];
            }
        }
    }

    // Enable/disable controls based on state
    BOOL isDefault = (_indentSelectedLang == nil);
    BOOL controlsEnabled = isDefault || hasOverride;
    for (NSView *sub in optBox.subviews) {
        if (sub.tag == 1320) continue; // skip the checkbox itself
        if ([sub isKindOfClass:[NSButton class]])
            [(NSButton *)sub setEnabled:controlsEnabled];
        else if ([sub isKindOfClass:[NSTextField class]] && [(NSTextField *)sub isEditable])
            [(NSTextField *)sub setEnabled:controlsEnabled];
    }
}

- (void)_useDefaultValueChanged:(NSButton *)sender {
    BOOL useDefault = (sender.state == NSControlStateValueOn);

    if (_indentSelectedLang != nil) {
        if (!useDefault) {
            // Create override immediately with current control values
            [self _saveIndentOverrideFromBox:sender.superview];
        } else if (useDefault) {
            // Remove per-language override
            NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
            NSMutableDictionary *overrides = [[ud dictionaryForKey:kPrefTabOverrides] mutableCopy] ?: [NSMutableDictionary dictionary];
            [overrides removeObjectForKey:_indentSelectedLang];
            [ud setObject:overrides forKey:kPrefTabOverrides];

            // Reload to show the effective settings (from langs.xml or global)
            NSInteger tabSize = 4; BOOL useTabs = YES; BOOL hasOvr = NO;
            [self _getIndentForLang:_indentSelectedLang tabSize:&tabSize useTabs:&useTabs hasOverride:&hasOvr];
            NSView *box = sender.superview;
            for (NSView *sub in box.subviews) {
                if (sub.tag == 100 && [sub isKindOfClass:[NSTextField class]])
                    [(NSTextField *)sub setIntegerValue:tabSize];
                else if (sub.tag == 1301 && [sub isKindOfClass:[NSButton class]])
                    [(NSButton *)sub setState:useTabs ? NSControlStateValueOn : NSControlStateValueOff];
                else if (sub.tag == 1302 && [sub isKindOfClass:[NSButton class]])
                    [(NSButton *)sub setState:useTabs ? NSControlStateValueOff : NSControlStateValueOn];
            }
        }
    }

    // Enable/disable controls
    NSView *box = sender.superview;
    for (NSView *sub in box.subviews) {
        if (sub == sender) continue;
        if ([sub isKindOfClass:[NSButton class]])
            [(NSButton *)sub setEnabled:!useDefault];
        else if ([sub isKindOfClass:[NSTextField class]] && [(NSTextField *)sub isEditable])
            [(NSTextField *)sub setEnabled:!useDefault];
    }

    [[NSNotificationCenter defaultCenter] postNotificationName:@"NPPPreferencesChanged" object:nil];
}

- (void)_saveIndentOverrideFromBox:(NSView *)box {
    // Read current values from the controls in the box
    NSInteger tabSize = 4;
    BOOL useTabs = YES;
    for (NSView *sub in box.subviews) {
        if (sub.tag == 100 && [sub isKindOfClass:[NSTextField class]])
            tabSize = [(NSTextField *)sub integerValue];
        else if (sub.tag == 1301 && [sub isKindOfClass:[NSButton class]])
            useTabs = ([(NSButton *)sub state] == NSControlStateValueOn);
    }
    if (tabSize < 1) tabSize = 4;

    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    if (_indentSelectedLang != nil && _indentSelectedLang.length > 0) {
        // Per-language override
        NSMutableDictionary *overrides = [[ud dictionaryForKey:kPrefTabOverrides] mutableCopy] ?: [NSMutableDictionary dictionary];
        overrides[_indentSelectedLang] = @{@"tabSize": @(tabSize), @"useTabs": @(useTabs)};
        [ud setObject:overrides forKey:kPrefTabOverrides];
    } else {
        // [Default] or Normal text → save to global
        [ud setInteger:tabSize forKey:kPrefTabWidth];
        [ud setBool:useTabs forKey:kPrefUseTabs];
    }
}

- (void)_indentUsingChanged:(NSButton *)sender {
    BOOL useTabs = (sender.tag == 1301);

    // Update sibling radio in the same NSBox
    NSView *box = sender.superview;
    for (NSView *sub in box.subviews) {
        if ([sub isKindOfClass:[NSButton class]] && sub != sender) {
            NSButton *btn = (NSButton *)sub;
            if (btn.tag == 1301 || btn.tag == 1302)
                btn.state = (btn == sender) ? NSControlStateValueOn : NSControlStateValueOff;
        }
    }

    // Save to appropriate target (global or per-language)
    if (_indentSelectedLang == nil) {
        // [Default] selected → save to global
        [[NSUserDefaults standardUserDefaults] setBool:useTabs forKey:kPrefUseTabs];
    } else {
        [self _saveIndentOverrideFromBox:box];
    }

    [[NSNotificationCenter defaultCenter] postNotificationName:@"NPPPreferencesChanged" object:nil];
}

- (void)_autoIndentChanged:(NSButton *)sender {
    // 1310=None(0), 1311=Basic(2), 1312=Advanced(1)
    NSInteger mode = 0;
    if (sender.tag == 1311) mode = 2;
    else if (sender.tag == 1312) mode = 1;

    [[NSUserDefaults standardUserDefaults] setInteger:mode forKey:kPrefAutoIndent];

    // Update sibling radios in the NSBox
    NSView *box = sender.superview;
    for (NSView *sub in box.subviews) {
        if ([sub isKindOfClass:[NSButton class]] && sub != sender) {
            [(NSButton *)sub setState:NSControlStateValueOff];
        }
    }
    [[NSNotificationCenter defaultCenter] postNotificationName:@"NPPPreferencesChanged" object:nil];
}

#pragma mark - Tab Bar Page

- (NSView *)_buildTabBarPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    CGFloat y = 380;
    NppLocalizer *loc = [NppLocalizer shared];

    NSArray *checks = @[
        @[[loc translate:@"Show close button on tabs"],        @800, kPrefTabCloseButton],
        @[[loc translate:@"Double-click to close tab"],        @801, kPrefDoubleClickTabClose],
    ];
    for (NSArray *def in checks) {
        NSButton *chk = [NSButton checkboxWithTitle:def[0] target:self action:@selector(prefChanged:)];
        chk.frame = NSMakeRect(20, y, 350, 20);
        chk.state = [ud boolForKey:def[2]] ? NSControlStateValueOn : NSControlStateValueOff;
        chk.tag = [def[1] integerValue];
        [v addSubview:chk];
        y -= 28;
    }

    y -= 8;
    NSTextField *mwLabel = [NSTextField labelWithString:[loc translate:@"Max tab width (pixels):"]];
    mwLabel.frame = NSMakeRect(20, y, 170, 20);
    [v addSubview:mwLabel];
    NSTextField *mwField = [[NSTextField alloc] initWithFrame:NSMakeRect(200, y-2, 60, 22)];
    mwField.integerValue = [ud integerForKey:kPrefTabMaxLabelWidth];
    mwField.tag = 802; mwField.target = self; mwField.action = @selector(prefChanged:);
    [v addSubview:mwField];

    return v;
}

#pragma mark - Margins Page

- (NSView *)_buildMarginsPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    CGFloat y = 380;
    NppLocalizer *loc = [NppLocalizer shared];

    // ── Edge Column ──
    NSTextField *edgeSection = [NSTextField labelWithString:[loc translate:@"Vertical Edge"]];
    edgeSection.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    edgeSection.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:edgeSection];
    y -= 28;

    NSTextField *emLabel = [NSTextField labelWithString:[loc translate:@"Edge mode:"]];
    emLabel.frame = NSMakeRect(20, y, 90, 20);
    [v addSubview:emLabel];
    NSPopUpButton *emPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(120, y-2, 160, 26) pullsDown:NO];
    [emPopup addItemsWithTitles:@[[loc translate:@"Off"], [loc translate:@"Line"], [loc translate:@"Background"]]];
    [emPopup selectItemAtIndex:[ud integerForKey:kPrefEdgeMode]];
    emPopup.tag = 1101; emPopup.target = self; emPopup.action = @selector(prefChanged:);
    [v addSubview:emPopup];
    y -= 30;

    NSTextField *ecLabel = [NSTextField labelWithString:[loc translate:@"Edge column:"]];
    ecLabel.frame = NSMakeRect(20, y, 100, 20);
    [v addSubview:ecLabel];
    NSTextField *ecField = [[NSTextField alloc] initWithFrame:NSMakeRect(130, y-2, 50, 22)];
    ecField.integerValue = [ud integerForKey:kPrefEdgeColumn];
    ecField.tag = 1100; ecField.target = self; ecField.action = @selector(prefChanged:);
    [v addSubview:ecField];
    y -= 36;

    // ── Fold Margin Style ──
    NSTextField *foldSection = [NSTextField labelWithString:[loc translate:@"Fold Margin Style"]];
    foldSection.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    foldSection.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:foldSection];
    y -= 28;

    NSPopUpButton *foldPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(20, y-2, 180, 26) pullsDown:NO];
    [foldPopup addItemsWithTitles:@[[loc translate:@"Box tree"], [loc translate:@"Circle tree"], [loc translate:@"Arrow"], [loc translate:@"Simple +/-"], [loc translate:@"None"]]];
    [foldPopup selectItemAtIndex:[ud integerForKey:kPrefFoldStyle]];
    foldPopup.tag = 1104; foldPopup.target = self; foldPopup.action = @selector(prefChanged:);
    [v addSubview:foldPopup];
    y -= 36;

    // ── Line Numbers ──
    NSButton *dynWidth = [NSButton checkboxWithTitle:[loc translate:@"Dynamic line number width"]
                                              target:self action:@selector(prefChanged:)];
    dynWidth.frame = NSMakeRect(20, y, 350, 20);
    dynWidth.state = [ud boolForKey:kPrefLineNumDynWidth] ? NSControlStateValueOn : NSControlStateValueOff;
    dynWidth.tag = 1105;
    [v addSubview:dynWidth];
    y -= 36;

    // ── Padding ──
    NSTextField *padSection = [NSTextField labelWithString:[loc translate:@"Padding"]];
    padSection.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    padSection.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:padSection];
    y -= 28;

    NSTextField *plLabel = [NSTextField labelWithString:[loc translate:@"Left:"]];
    plLabel.frame = NSMakeRect(20, y, 40, 20);
    [v addSubview:plLabel];
    NSTextField *plField = [[NSTextField alloc] initWithFrame:NSMakeRect(65, y-2, 50, 22)];
    plField.integerValue = [ud integerForKey:kPrefPaddingLeft];
    plField.tag = 1102; plField.target = self; plField.action = @selector(prefChanged:);
    [v addSubview:plField];

    NSTextField *prLabel = [NSTextField labelWithString:[loc translate:@"Right:"]];
    prLabel.frame = NSMakeRect(140, y, 50, 20);
    [v addSubview:prLabel];
    NSTextField *prField = [[NSTextField alloc] initWithFrame:NSMakeRect(195, y-2, 50, 22)];
    prField.integerValue = [ud integerForKey:kPrefPaddingRight];
    prField.tag = 1103; prField.target = self; prField.action = @selector(prefChanged:);
    [v addSubview:prField];

    return v;
}

#pragma mark - Dark Mode Page

- (NSView *)_buildDarkModePage {
    NSView *v = [[NSView alloc] init];
    CGFloat y = 380;

    NppLocalizer *loc = [NppLocalizer shared];
    NSTextField *dmLabel = [NSTextField labelWithString:[loc translate:@"Appearance"]];
    dmLabel.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    dmLabel.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:dmLabel];
    y -= 32;

    NSArray *titles = @[[loc translate:@"Auto (Follow System)"], [loc translate:@"Light"], [loc translate:@"Dark"]];
    for (NSInteger i = 0; i < 3; i++) {
        NSButton *radio = [NSButton radioButtonWithTitle:titles[i] target:self action:@selector(_darkModeRadioChanged:)];
        radio.frame = NSMakeRect(20, y, 300, 20);
        radio.tag = 500 + i;
        radio.state = ([NppThemeManager shared].mode == i) ? NSControlStateValueOn : NSControlStateValueOff;
        [v addSubview:radio];
        y -= 24;
    }

    return v;
}

- (void)_darkModeRadioChanged:(id)sender {
    NSInteger mode = [(NSButton *)sender tag] - 500;
    // Deselect other radios
    NSView *page = [(NSButton *)sender superview];
    for (NSView *sub in page.subviews) {
        if ([sub isKindOfClass:[NSButton class]] && [(NSButton *)sub tag] >= 500 && [(NSButton *)sub tag] <= 502) {
            [(NSButton *)sub setState:((NSButton *)sub).tag == [(NSButton *)sender tag]
                ? NSControlStateValueOn : NSControlStateValueOff];
        }
    }
    [NppThemeManager shared].mode = (NppDarkModeOption)mode;

    // Switch theme to match dark/light mode
    NPPStyleStore *store = [NPPStyleStore sharedStore];
    BOOL effectiveDark = (mode == NppDarkModeDark) ||
        (mode == NppDarkModeAuto && [NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:
            @[NSAppearanceNameDarkAqua]] != nil);
    NSString *targetTheme = effectiveDark ? @"DarkModeDefault" : @"Default (stylers.xml)";
    if (![store.activeThemeName isEqualToString:targetTheme]) {
        NSArray *lexers = [store lexersForTheme:targetTheme];
        [store commitLexers:lexers themeName:targetTheme];
    }
}

#pragma mark - Toolbar Page

- (NSView *)_buildToolbarPage {
    NSView *v = [[NSView alloc] init];
    CGFloat y = 380;

    NppLocalizer *loc = [NppLocalizer shared];
    NSTextField *label = [NSTextField labelWithString:[loc translate:@"Toolbar Icon Style"]];
    label.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    label.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:label];
    y -= 32;

    NSArray *titles = @[[loc translate:@"Fluent (Modern)"], [loc translate:@"Classic (Windows)"]];
    NppToolbarIconStyle current = [NppThemeManager shared].toolbarIconStyle;
    for (NSInteger i = 0; i < 2; i++) {
        NSButton *radio = [NSButton radioButtonWithTitle:titles[i] target:self action:@selector(_toolbarIconStyleRadioChanged:)];
        radio.frame = NSMakeRect(20, y, 300, 20);
        radio.tag = 600 + i;
        radio.state = (current == i) ? NSControlStateValueOn : NSControlStateValueOff;
        [v addSubview:radio];
        y -= 24;
    }

    return v;
}

- (void)_toolbarIconStyleRadioChanged:(id)sender {
    NSInteger style = [(NSButton *)sender tag] - 600;
    NSView *page = [(NSButton *)sender superview];
    for (NSView *sub in page.subviews) {
        if ([sub isKindOfClass:[NSButton class]] && [(NSButton *)sub tag] >= 600 && [(NSButton *)sub tag] <= 601) {
            [(NSButton *)sub setState:((NSButton *)sub).tag == [(NSButton *)sender tag]
                ? NSControlStateValueOn : NSControlStateValueOff];
        }
    }
    [NppThemeManager shared].toolbarIconStyle = (NppToolbarIconStyle)style;
}

#pragma mark - New Document Page

- (NSView *)_buildNewDocPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    CGFloat y = 380;

    NppLocalizer *loc = [NppLocalizer shared];
    NSTextField *eolLabel = [NSTextField labelWithString:[loc translate:@"Default line ending:"]];
    eolLabel.frame = NSMakeRect(20, y, 150, 20);
    [v addSubview:eolLabel];

    NSPopUpButton *eolPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(180, y-2, 180, 26) pullsDown:NO];
    [eolPopup addItemsWithTitles:@[[loc translate:@"Windows (CRLF)"], [loc translate:@"Unix (LF)"], [loc translate:@"Mac (CR)"]]];
    [eolPopup selectItemAtIndex:[ud integerForKey:kPrefEOLType]];
    eolPopup.tag = 200;
    eolPopup.target = self;
    eolPopup.action = @selector(prefChanged:);
    [v addSubview:eolPopup];
    y -= 36;

    NSTextField *encLabel = [NSTextField labelWithString:[loc translate:@"Default encoding:"]];
    encLabel.frame = NSMakeRect(20, y, 150, 20);
    [v addSubview:encLabel];

    NSPopUpButton *encPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(180, y-2, 180, 26) pullsDown:NO];
    [encPopup addItemsWithTitles:@[@"UTF-8", @"Latin-1 (ISO-8859-1)"]];
    [encPopup selectItemAtIndex:[ud integerForKey:kPrefEncoding]];
    encPopup.tag = 201;
    encPopup.target = self;
    encPopup.action = @selector(prefChanged:);
    [v addSubview:encPopup];

    return v;
}

#pragma mark - Backup Page

- (NSView *)_buildBackupPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    CGFloat y = 380;

    NSButton *autoBackup = [NSButton checkboxWithTitle:[[NppLocalizer shared] translate:@"Enable auto-backup"]
                                                target:self action:@selector(prefChanged:)];
    autoBackup.frame = NSMakeRect(20, y, 350, 20);
    autoBackup.state = [ud boolForKey:kPrefAutoBackup] ? NSControlStateValueOn : NSControlStateValueOff;
    autoBackup.tag = 300;
    [v addSubview:autoBackup];
    y -= 30;

    NSTextField *intLabel = [NSTextField labelWithString:[[NppLocalizer shared] translate:@"Backup interval (seconds):"]];
    intLabel.frame = NSMakeRect(20, y, 200, 20);
    [v addSubview:intLabel];

    NSTextField *intField = [[NSTextField alloc] initWithFrame:NSMakeRect(230, y-2, 60, 22)];
    intField.integerValue = [ud integerForKey:kPrefBackupInterval];
    intField.tag = 301;
    intField.target = self;
    intField.action = @selector(prefChanged:);
    [v addSubview:intField];
    y -= 36;

    NSTextField *backupDirLabel = [NSTextField labelWithString:[[NppLocalizer shared] translate:@"Backup location:"]];
    backupDirLabel.frame = NSMakeRect(20, y, 140, 20);
    [v addSubview:backupDirLabel];
    y -= 20;

    NSTextField *backupPath = [NSTextField labelWithString:
        [NSHomeDirectory() stringByAppendingPathComponent:@".notepad++/backup/"]];
    backupPath.frame = NSMakeRect(20, y, 400, 20);
    backupPath.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    [v addSubview:backupPath];

    return v;
}

#pragma mark - Auto-Completion Page

- (NSView *)_buildAutoCompletionPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    CGFloat y = 380;
    NppLocalizer *loc = [NppLocalizer shared];

    NSArray *checks = @[
        @[[loc translate:@"Enable auto-completion on each input"],     @600, kPrefAutoCompleteEnable],
        @[[loc translate:@"Function parameters hint on input"],        @602, kPrefFuncParamsHint],
    ];
    for (NSArray *def in checks) {
        NSButton *chk = [NSButton checkboxWithTitle:def[0] target:self action:@selector(prefChanged:)];
        chk.frame = NSMakeRect(20, y, 400, 20);
        chk.state = [ud boolForKey:def[2]] ? NSControlStateValueOn : NSControlStateValueOff;
        chk.tag = [def[1] integerValue];
        [v addSubview:chk];
        y -= 28;
    }

    y -= 4;
    NSTextField *minLabel = [NSTextField labelWithString:[loc translate:@"From Nth character:"]];
    minLabel.frame = NSMakeRect(20, y, 160, 20);
    [v addSubview:minLabel];

    NSTextField *minField = [[NSTextField alloc] initWithFrame:NSMakeRect(190, y-2, 50, 22)];
    minField.integerValue = [ud integerForKey:kPrefAutoCompleteMinChars];
    minField.tag = 601;
    minField.target = self;
    minField.action = @selector(prefChanged:);
    [v addSubview:minField];

    return v;
}

#pragma mark - Searching Page

- (NSView *)_buildSearchingPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    CGFloat y = 380;
    NppLocalizer *loc = [NppLocalizer shared];

    NSArray *checks = @[
        @[[loc translate:@"Enable smart highlighting"],              @1000, kPrefSmartHighlight],
        @[[loc translate:@"Smart highlighting: match case"],          @1002, kPrefSmartHiliteCase],
        @[[loc translate:@"Smart highlighting: whole word only"],     @1003, kPrefSmartHiliteWord],
        @[[loc translate:@"Fill find field with selected text"],      @1001, kPrefFillFindWithSelection],
        @[[loc translate:@"Use monospaced font in Find dialog"],      @1004, kPrefMonoFontFind],
        @[[loc translate:@"Confirm Replace All in open documents"],   @1005, kPrefConfirmReplaceAll],
        @[[loc translate:@"Replace: don't move to next occurrence"],  @1006, kPrefReplaceAndStop],
    ];
    for (NSArray *def in checks) {
        NSButton *chk = [NSButton checkboxWithTitle:def[0] target:self action:@selector(prefChanged:)];
        chk.frame = NSMakeRect(20, y, 400, 20);
        chk.state = [ud boolForKey:def[2]] ? NSControlStateValueOn : NSControlStateValueOff;
        chk.tag = [def[1] integerValue];
        [v addSubview:chk];
        y -= 28;
    }

    y -= 8;
    NSTextField *threshLabel = [NSTextField labelWithString:[loc translate:@"In-selection auto-check threshold (bytes):"]];
    threshLabel.frame = NSMakeRect(20, y, 300, 20);
    [v addSubview:threshLabel];
    NSTextField *threshField = [[NSTextField alloc] initWithFrame:NSMakeRect(330, y-2, 60, 22)];
    threshField.integerValue = [ud integerForKey:kPrefInSelThreshold];
    threshField.tag = 1007; threshField.target = self; threshField.action = @selector(prefChanged:);
    [v addSubview:threshField];

    return v;
}

// Search Engine page removed — merged into Searching

#pragma mark - MISC. Page

- (NSView *)_buildMiscPage {
    NSView *v = [[NSView alloc] init];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    CGFloat y = 380;
    NppLocalizer *loc = [NppLocalizer shared];

    NSArray *checks = @[
        @[[loc translate:@"Mute all sounds"],                          @1200, kPrefMuteSounds],
        @[[loc translate:@"Confirm before Save All"],                  @1201, kPrefSaveAllConfirm],
        @[[loc translate:@"Reverse default date/time order"],          @1202, kPrefDateTimeReverse],
        @[[loc translate:@"Keep absent file entries in session"],      @1203, kPrefKeepAbsentSession],
        @[[loc translate:@"Remember panel visibility across sessions"], @1204, kPrefPanelKeepState],
        @[[loc translate:@"Use XML-based function list parsers"],      @1205, kPrefFuncListUseXML],
        @[@"Route plugin messages to split view editors",            @1206, kPrefPluginSplitViewRouting],
    ];
    for (NSArray *def in checks) {
        NSButton *chk = [NSButton checkboxWithTitle:def[0] target:self action:@selector(prefChanged:)];
        chk.frame = NSMakeRect(20, y, 400, 20);
        chk.state = [ud boolForKey:def[2]] ? NSControlStateValueOn : NSControlStateValueOff;
        chk.tag = [def[1] integerValue];
        [v addSubview:chk];
        y -= 28;
    }

    return v;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Language Popup Helper
// ═══════════════════════════════════════════════════════════════════════════════

- (void)_rebuildLanguagePopup {
    if (!_languagePopup) return;

    NSDictionary<NSString *, NSString *> *langMap = [NppLocalizer availableLanguagesMap];
    NSArray<NSString *> *names = [[langMap allKeys]
        sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];

    [_languagePopup removeAllItems];
    [_languagePopup addItemsWithTitles:names];

    NSString *currentFile = [NppLocalizer shared].currentLanguageFile;
    for (NSString *name in names) {
        if ([langMap[name].lowercaseString isEqualToString:currentFile.lowercaseString]) {
            [_languagePopup selectItemWithTitle:name];
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Actions
// ═══════════════════════════════════════════════════════════════════════════════

- (void)prefChanged:(id)sender {
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    NSInteger tag = [(NSControl *)sender tag];

    switch (tag) {
        case 100: {
            if (_indentSelectedLang == nil) {
                [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefTabWidth];
            } else {
                [self _saveIndentOverrideFromBox:[(NSTextField *)sender superview]];
            }
            break;
        }
        case 103: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefShowLineNumbers]; break;
        case 105: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefHighlightCurrentLine]; break;
        case 200: [ud setInteger:[(NSPopUpButton *)sender indexOfSelectedItem] forKey:kPrefEOLType]; break;
        case 201: [ud setInteger:[(NSPopUpButton *)sender indexOfSelectedItem] forKey:kPrefEncoding]; break;
        case 300: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefAutoBackup]; break;
        case 301: [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefBackupInterval]; break;
        case 400: {
            NSPopUpButton *popup = (NSPopUpButton *)sender;
            NSString *selectedName = popup.selectedItem.title;
            if (selectedName.length > 0) {
                NSDictionary *langMap = [NppLocalizer availableLanguagesMap];
                NSString *stem = langMap[selectedName];
                if (stem) {
                    [[NppLocalizer shared] loadLanguageNamed:stem];
                }
            }
            return;
        }
        case 600: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefAutoCompleteEnable]; break;
        case 601: [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefAutoCompleteMinChars]; break;
        case 602: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefFuncParamsHint]; break;
        // Editor settings
        case 700: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefAutoCloseBrackets]; break;
        case 701: [ud setInteger:[(NSPopUpButton *)sender indexOfSelectedItem] + 1 forKey:kPrefCaretWidth]; break;
        case 702: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefVirtualSpace]; break;
        case 703: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefScrollBeyondLastLine]; break;
        case 704: [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefCaretBlinkRate]; break;
        case 705: [ud setInteger:[(NSPopUpButton *)sender indexOfSelectedItem] forKey:kPrefFontQuality]; break;
        case 706: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefCopyLineNoSelection]; break;
        // Tab Bar settings
        case 800: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefTabCloseButton]; break;
        case 801: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefDoubleClickTabClose]; break;
        case 802: [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefTabMaxLabelWidth]; break;
        // General
        case 900: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefShowFullPathInTitle]; break;
        // Searching
        case 1000: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefSmartHighlight]; break;
        case 1001: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefFillFindWithSelection]; break;
        case 1002: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefSmartHiliteCase]; break;
        case 1003: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefSmartHiliteWord]; break;
        case 1004: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefMonoFontFind]; break;
        case 1005: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefConfirmReplaceAll]; break;
        case 1006: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefReplaceAndStop]; break;
        case 1007: [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefInSelThreshold]; break;
        // General
        case 901: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefShowStatusBar]; break;
        // Editor
        case 707: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefRightClickKeepsSel]; break;
        case 708: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefDisableTextDragDrop]; break;
        case 709: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefShowBookmarkMargin]; break;
        case 710: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefShowEOL]; break;
        case 711: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefShowWhitespace]; break;
        // Margins
        case 1100: [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefEdgeColumn]; break;
        case 1101: [ud setInteger:[(NSPopUpButton *)sender indexOfSelectedItem] forKey:kPrefEdgeMode]; break;
        case 1102: [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefPaddingLeft]; break;
        case 1103: [ud setInteger:[(NSTextField *)sender integerValue] forKey:kPrefPaddingRight]; break;
        case 1104: [ud setInteger:[(NSPopUpButton *)sender indexOfSelectedItem] forKey:kPrefFoldStyle]; break;
        case 1105: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefLineNumDynWidth]; break;
        // MISC
        case 1200: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefMuteSounds]; break;
        case 1201: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefSaveAllConfirm]; break;
        case 1202: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefDateTimeReverse]; break;
        case 1203: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefKeepAbsentSession]; break;
        case 1204: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefPanelKeepState]; break;
        case 1205: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefFuncListUseXML]; break;
        case 1206: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefPluginSplitViewRouting]; break;
        // Indentation
        case 1303: [ud setBool:[(NSButton *)sender state] == NSControlStateValueOn forKey:kPrefBackspaceUnindent]; break;
    }

    [[NSNotificationCenter defaultCenter] postNotificationName:@"NPPPreferencesChanged" object:nil];
}

- (void)closePrefs:(id)sender {
    [self.window orderOut:nil];
}

@end
