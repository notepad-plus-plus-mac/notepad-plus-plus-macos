#import "DocumentListPanel.h"
#import "EditorView.h"
#import "StyleConfiguratorWindowController.h"
#import "NppThemeManager.h"
#import "NppLocalizer.h"

// The panel body is the table view, flush to edges; the title bar + close
// button + separator are supplied by PanelFrame.
//
// Columns: Name (floppy save icon + base filename, no extension), Ext.,
// Path. Ext./Path are toggleable via an empty-area right-click menu and
// their visibility persists in NSUserDefaults. Click a header to sort;
// the sort choice is kept for the running session (this panel object
// lives for the whole session). Right-clicking a row shows the same
// context menu as an editor tab.

static NSString *const kDocListShowExt  = @"DocList_ShowExt";
static NSString *const kDocListShowPath = @"DocList_ShowPath";

// Splits an editor into the three display fields. A saved file is split
// into base name + extension; an untitled buffer has neither extension
// nor path.
static void _docFields(EditorView *ed, NSString **outName,
                       NSString **outExt, NSString **outPath) {
    NSString *fp = ed.filePath;
    if (fp.length) {
        NSString *last = fp.lastPathComponent;
        NSString *e    = last.pathExtension;
        *outName = e.length ? [last stringByDeletingPathExtension] : last;
        *outExt  = e.length ? [@"." stringByAppendingString:e] : @"";
        *outPath = fp.stringByDeletingLastPathComponent;
    } else {
        *outName = ed.displayName;
        *outExt  = @"";
        *outPath = @"";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Table view subclass (routes right-click to the panel)

@interface _DocListTableView : NSTableView
@property (nonatomic, weak) DocumentListPanel *ownerPanel;
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Private bridge

@interface DocumentListPanel ()
/// Builds the context menu for a right-click at `row` (row < 0 → the
/// empty-area column-toggle menu). For a valid row it first selects that
/// editor so the tab-context-menu commands target it.
- (NSMenu *)contextMenuForRow:(NSInteger)row;
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - DocumentListPanel

@implementation DocumentListPanel {
    TabManager   *_tabManager;
    NSScrollView *_scrollView;
    NSTableView  *_tableView;
    NSArray<EditorView *> *_items;
    CGFloat       _panelFontSize;
    BOOL          _showExt;
    BOOL          _showPath;
    NSString     *_sortKey;        // nil → tab order; else "name"/"ext"/"path"
    BOOL          _sortAscending;
}

- (instancetype)initWithTabManager:(TabManager *)tabManager {
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        _tabManager = tabManager;
        _items = @[];
        _sortKey = nil;
        _sortAscending = YES;
        { CGFloat z = [[NSUserDefaults standardUserDefaults] floatForKey:@"PanelZoom_DocumentList"]; _panelFontSize = z >= 8 ? z : 11; }
        [self _buildLayout];
    }
    return self;
}

- (void)dealloc { [[NSNotificationCenter defaultCenter] removeObserver:self]; }

- (void)_buildLayout {
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    id ve = [ud objectForKey:kDocListShowExt];  _showExt  = ve ? [ve boolValue] : YES;
    id vp = [ud objectForKey:kDocListShowPath]; _showPath = vp ? [vp boolValue] : YES;

    _scrollView = [[NSScrollView alloc] init];
    NSScrollView *scroll = _scrollView;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = NO;
    scroll.autohidesScrollers = YES;

    _DocListTableView *table = [[_DocListTableView alloc] init];
    table.ownerPanel = self;
    _tableView = table;
    _tableView.headerView = [[NSTableHeaderView alloc] init];
    _tableView.rowHeight = _panelFontSize + 8;
    _tableView.dataSource = self;
    _tableView.delegate = self;
    _tableView.allowsEmptySelection = YES;
    _tableView.allowsMultipleSelection = NO;
    _tableView.usesAlternatingRowBackgroundColors = NO;
    _tableView.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;

    NSTableColumn *nameCol = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    nameCol.title = [[NppLocalizer shared] translate:@"Name"];
    nameCol.width = 150; nameCol.minWidth = 60;
    nameCol.resizingMask = NSTableColumnUserResizingMask;
    nameCol.sortDescriptorPrototype = [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES];
    [_tableView addTableColumn:nameCol];

    NSTableColumn *extCol = [[NSTableColumn alloc] initWithIdentifier:@"ext"];
    extCol.title = [[NppLocalizer shared] translate:@"Ext."];
    extCol.width = 54; extCol.minWidth = 40;
    extCol.resizingMask = NSTableColumnUserResizingMask;
    extCol.sortDescriptorPrototype = [NSSortDescriptor sortDescriptorWithKey:@"ext" ascending:YES];
    extCol.hidden = !_showExt;
    [_tableView addTableColumn:extCol];

    NSTableColumn *pathCol = [[NSTableColumn alloc] initWithIdentifier:@"path"];
    pathCol.title = [[NppLocalizer shared] translate:@"Path"];
    pathCol.width = 150; pathCol.minWidth = 60;
    pathCol.resizingMask = NSTableColumnUserResizingMask;
    pathCol.sortDescriptorPrototype = [NSSortDescriptor sortDescriptorWithKey:@"path" ascending:YES];
    pathCol.hidden = !_showPath;
    [_tableView addTableColumn:pathCol];

    _tableView.sortDescriptors = @[];   // start in tab order, no sort arrow

    scroll.documentView = _tableView;
    [self addSubview:scroll];

    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor      constraintEqualToAnchor:self.topAnchor],
        [scroll.leadingAnchor  constraintEqualToAnchor:self.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [scroll.bottomAnchor   constraintEqualToAnchor:self.bottomAnchor],
    ]];

    _tableView.target = self;
    _tableView.action = @selector(_rowClicked:);

    [self _applyTheme];
    [[NSNotificationCenter defaultCenter]
        addObserver:self selector:@selector(_themeChanged:)
               name:@"NPPPreferencesChanged" object:nil];
}

- (void)_applyTheme {
    NSColor *bg = [[NPPStyleStore sharedStore] globalBg];
    _scrollView.backgroundColor = bg;
    _tableView.backgroundColor  = bg;
    [_tableView reloadData];   // refresh cell text colors + floppy icons
}

- (void)_themeChanged:(NSNotification *)note {
    [self _applyTheme];
}

// ── Model ─────────────────────────────────────────────────────────────────────

// Re-sorts _items in place according to the current sort column.
- (void)_resort {
    if (!_sortKey) return;                       // tab order
    NSString *key = _sortKey;
    BOOL asc = _sortAscending;
    _items = [_items sortedArrayUsingComparator:^NSComparisonResult(EditorView *a, EditorView *b) {
        NSString *na, *ea, *pa, *nb, *eb, *pb;
        _docFields(a, &na, &ea, &pa);
        _docFields(b, &nb, &eb, &pb);
        NSString *va = [key isEqualToString:@"ext"]  ? ea
                     : [key isEqualToString:@"path"] ? pa : na;
        NSString *vb = [key isEqualToString:@"ext"]  ? eb
                     : [key isEqualToString:@"path"] ? pb : nb;
        NSComparisonResult r = [va localizedStandardCompare:vb];
        if (r == NSOrderedSame && ![key isEqualToString:@"name"])
            r = [na localizedStandardCompare:nb];   // stable tie-break by name
        return asc ? r : (NSComparisonResult)(-r);
    }];
}

- (void)reloadData {
    _items = [_tabManager.allEditors copy];
    [self _resort];

    [_tableView reloadData];

    EditorView *current = _tabManager.currentEditor;
    NSUInteger idx = current ? [_items indexOfObject:current] : NSNotFound;
    if (idx != NSNotFound) {
        [_tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:idx]
                byExtendingSelection:NO];
        [_tableView scrollRowToVisible:(NSInteger)idx];
    } else {
        [_tableView deselectAll:nil];
    }
}

- (void)refreshModifiedStates {
    if (_items.count == 0) return;
    NSInteger nameCol = [_tableView columnWithIdentifier:@"name"];
    if (nameCol < 0) return;
    [_tableView reloadDataForRowIndexes:
        [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, _items.count)]
                          columnIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)nameCol]];
}

// ── NSTableViewDataSource ─────────────────────────────────────────────────────

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return (NSInteger)_items.count;
}

- (void)tableView:(NSTableView *)tableView
    sortDescriptorsDidChange:(NSArray<NSSortDescriptor *> *)oldDescriptors {
    NSSortDescriptor *sd = tableView.sortDescriptors.firstObject;
    if (sd) { _sortKey = sd.key; _sortAscending = sd.ascending; }
    else    { _sortKey = nil; }
    [self reloadData];
}

// ── NSTableViewDelegate ───────────────────────────────────────────────────────

- (NSTableCellView *)_makeNameCell {
    NSTableCellView *cv = [[NSTableCellView alloc] init];
    cv.identifier = @"DocNameCell";

    NSImageView *iv = [[NSImageView alloc] init];
    iv.translatesAutoresizingMaskIntoConstraints = NO;
    iv.imageScaling = NSImageScaleProportionallyUpOrDown;
    [cv addSubview:iv];
    cv.imageView = iv;

    NSTextField *tf = [NSTextField labelWithString:@""];
    tf.translatesAutoresizingMaskIntoConstraints = NO;
    tf.lineBreakMode = NSLineBreakByTruncatingTail;
    [cv addSubview:tf];
    cv.textField = tf;

    [NSLayoutConstraint activateConstraints:@[
        [iv.leadingAnchor  constraintEqualToAnchor:cv.leadingAnchor constant:3],
        [iv.centerYAnchor  constraintEqualToAnchor:cv.centerYAnchor],
        [iv.widthAnchor    constraintEqualToConstant:14],
        [iv.heightAnchor   constraintEqualToConstant:14],
        [tf.leadingAnchor  constraintEqualToAnchor:iv.trailingAnchor constant:4],
        [tf.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-3],
        [tf.centerYAnchor  constraintEqualToAnchor:cv.centerYAnchor],
    ]];
    return cv;
}

- (NSTableCellView *)_makeTextCell {
    NSTableCellView *cv = [[NSTableCellView alloc] init];
    cv.identifier = @"DocTextCell";

    NSTextField *tf = [NSTextField labelWithString:@""];
    tf.translatesAutoresizingMaskIntoConstraints = NO;
    tf.lineBreakMode = NSLineBreakByTruncatingTail;
    [cv addSubview:tf];
    cv.textField = tf;

    [NSLayoutConstraint activateConstraints:@[
        [tf.leadingAnchor  constraintEqualToAnchor:cv.leadingAnchor constant:4],
        [tf.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-3],
        [tf.centerYAnchor  constraintEqualToAnchor:cv.centerYAnchor],
    ]];
    return cv;
}

- (nullable NSView *)tableView:(NSTableView *)tableView
            viewForTableColumn:(nullable NSTableColumn *)tableColumn
                           row:(NSInteger)row {
    if (row < 0 || row >= (NSInteger)_items.count) return nil;
    EditorView *ed = _items[row];
    NSString *name, *ext, *path;
    _docFields(ed, &name, &ext, &path);

    NSColor  *fg      = [[NPPStyleStore sharedStore] globalFg];
    NSFont   *font    = [NSFont systemFontOfSize:_panelFontSize];
    NSString *fullTip = ed.filePath ?: ed.displayName;
    NSString *colId   = tableColumn.identifier;

    if ([colId isEqualToString:@"name"]) {
        NSTableCellView *cv = [tableView makeViewWithIdentifier:@"DocNameCell" owner:self];
        if (!cv) cv = [self _makeNameCell];
        cv.toolTip = fullTip;
        cv.imageView.image = [[NppThemeManager shared]
            toolbarIconNamed:(ed.isModified ? @"saveFileRed" : @"saveFile")];
        cv.textField.stringValue = name;
        cv.textField.textColor   = fg;
        cv.textField.font        = font;
        return cv;
    }

    NSTableCellView *cv = [tableView makeViewWithIdentifier:@"DocTextCell" owner:self];
    if (!cv) cv = [self _makeTextCell];
    cv.toolTip = fullTip;
    cv.textField.stringValue = [colId isEqualToString:@"ext"] ? ext : path;
    cv.textField.textColor   = fg;
    cv.textField.font        = font;
    return cv;
}

// ── Row click (left-click focuses the file — unchanged behaviour) ─────────────

- (void)_rowClicked:(id)sender {
    NSInteger row = _tableView.clickedRow;
    if (row >= 0 && row < (NSInteger)_items.count)
        [self _selectEditorAtRow:row];
}

// Maps a panel row (which may be sorted) back to the tab manager's index.
- (void)_selectEditorAtRow:(NSInteger)row {
    EditorView *ed = _items[row];
    NSUInteger tabIdx = [_tabManager.allEditors indexOfObject:ed];
    if (tabIdx != NSNotFound)
        [_tabManager selectTabAtIndex:(NSInteger)tabIdx];
}

// ── Context menus ─────────────────────────────────────────────────────────────

- (NSMenu *)contextMenuForRow:(NSInteger)row {
    if (row >= 0 && row < (NSInteger)_items.count) {
        // Right-click selects the row's editor first so the tab context
        // menu's commands (which act on the current document) target it —
        // matching how right-clicking a tab behaves.
        [self _selectEditorAtRow:row];
        return [_tabManager.tabBar buildTabContextMenu];
    }
    return [self _emptyAreaMenu];
}

- (NSMenu *)_emptyAreaMenu {
    NppLocalizer *loc = [NppLocalizer shared];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];

    NSMenuItem *extItem = [[NSMenuItem alloc] initWithTitle:[loc translate:@"Ext."]
                                                     action:@selector(_toggleExtColumn:)
                                              keyEquivalent:@""];
    extItem.target = self;
    extItem.state  = _showExt ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:extItem];

    NSMenuItem *pathItem = [[NSMenuItem alloc] initWithTitle:[loc translate:@"Path"]
                                                      action:@selector(_togglePathColumn:)
                                               keyEquivalent:@""];
    pathItem.target = self;
    pathItem.state  = _showPath ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:pathItem];

    return menu;
}

- (void)_toggleExtColumn:(id)sender {
    _showExt = !_showExt;
    [[NSUserDefaults standardUserDefaults] setBool:_showExt forKey:kDocListShowExt];
    [_tableView tableColumnWithIdentifier:@"ext"].hidden = !_showExt;
}

- (void)_togglePathColumn:(id)sender {
    _showPath = !_showPath;
    [[NSUserDefaults standardUserDefaults] setBool:_showPath forKey:kDocListShowPath];
    [_tableView tableColumnWithIdentifier:@"path"].hidden = !_showPath;
}

#pragma mark - Panel Zoom

- (void)_saveZoom { [[NSUserDefaults standardUserDefaults] setFloat:_panelFontSize forKey:@"PanelZoom_DocumentList"]; }
- (void)panelZoomIn    { _panelFontSize = MIN(_panelFontSize + 1, 28); _tableView.rowHeight = _panelFontSize + 8; [_tableView reloadData]; [self _saveZoom]; }
- (void)panelZoomOut   { _panelFontSize = MAX(_panelFontSize - 1, 8);  _tableView.rowHeight = _panelFontSize + 8; [_tableView reloadData]; [self _saveZoom]; }
- (void)panelZoomReset { _panelFontSize = 11; _tableView.rowHeight = _panelFontSize + 8; [_tableView reloadData]; [self _saveZoom]; }
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - _DocListTableView

@implementation _DocListTableView

- (NSMenu *)menuForEvent:(NSEvent *)event {
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger row = [self rowAtPoint:pt];
    return [_ownerPanel contextMenuForRow:row];
}

@end
