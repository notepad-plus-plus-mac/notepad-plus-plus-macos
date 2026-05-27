#import "NPPBatchDialog.h"
#import "NPPBatchRunner.h"
#import "MainWindowController.h"
#import "NppLocalizer.h"
#import <objc/runtime.h>

// loadMacrosFromShortcutsXML is a file-private helper inside
// MainWindowController.mm. We re-declare it here with the same C signature so
// the linker resolves it — keeping the shortcuts-XML reader in one place.
extern NSArray<NSDictionary *> *loadMacrosFromShortcutsXML(void);

// Filter a pre-built list of absolute paths against the same extension /
// size predicates the folder enumerator uses. Used by files-mode (Project
// Panel entry) where the file set is fixed; folder-mode goes through
// enumerateFiles() below instead.
static NSArray<NSString *> *filterFiles(NSArray<NSString *> *input,
                                        NSArray<NSString *> *globs,
                                        NSInteger maxSizeBytes) {
    NSMutableArray<NSPredicate *> *preds = [NSMutableArray array];
    for (NSString *g in globs) {
        NSString *t = [g stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (t.length) [preds addObject:[NSPredicate predicateWithFormat:@"SELF LIKE[c] %@", t]];
    }
    const BOOL anyGlob = preds.count > 0;

    NSFileManager *fm = [NSFileManager defaultManager];
    NSMutableArray<NSString *> *out = [NSMutableArray arrayWithCapacity:input.count];
    for (NSString *path in input) {
        if (anyGlob) {
            BOOL matches = NO;
            for (NSPredicate *p in preds) {
                if ([p evaluateWithObject:path.lastPathComponent]) { matches = YES; break; }
            }
            if (!matches) continue;
        }
        if (maxSizeBytes > 0) {
            NSDictionary *attrs = [fm attributesOfItemAtPath:path error:nil];
            NSNumber *sz = attrs[NSFileSize];
            if (sz && sz.longLongValue > maxSizeBytes) continue;
        }
        [out addObject:path];
    }
    return out;
}

// File enumeration helper. Walks the folder tree honoring the dialog's
// recurse/hidden/extension/size filters. Identical filter semantics to
// +[SearchEngine findInDirectory:options:...] (the Find in Files path) so
// users get predictable matches across both features.
static NSArray<NSString *> *enumerateFiles(NSString *root,
                                            NSArray<NSString *> *globs,
                                            BOOL recurse,
                                            BOOL includeHidden,
                                            NSInteger maxSizeBytes) {
    NSMutableArray<NSString *> *out = [NSMutableArray array];
    NSFileManager *fm = [NSFileManager defaultManager];
    NSDirectoryEnumerator *en = [fm enumeratorAtPath:root];
    if (!en) return out;
    if (!recurse) [en skipDescendants];

    // Compile globs once.
    NSMutableArray<NSPredicate *> *preds = [NSMutableArray array];
    for (NSString *g in globs) {
        NSString *t = [g stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (t.length) [preds addObject:[NSPredicate predicateWithFormat:@"SELF LIKE[c] %@", t]];
    }
    const BOOL anyGlob = preds.count > 0;

    NSString *rel;
    while ((rel = [en nextObject])) {
        NSString *full = [root stringByAppendingPathComponent:rel];
        BOOL isDir = NO;
        if (![fm fileExistsAtPath:full isDirectory:&isDir]) continue;

        if (isDir) {
            // Hidden directories are skipped EARLY (skipDescendants) to avoid
            // recursing into them at all. Saves enormous time on .git etc.
            if (!includeHidden && [rel.lastPathComponent hasPrefix:@"."]) {
                [en skipDescendants];
            }
            // If !recurse the enumerator was already told to not descend at
            // the root level (skipDescendants above). Directories themselves
            // never count as matches.
            continue;
        }

        if (!includeHidden && [rel.lastPathComponent hasPrefix:@"."]) continue;

        if (anyGlob) {
            BOOL matches = NO;
            for (NSPredicate *p in preds) {
                if ([p evaluateWithObject:rel.lastPathComponent]) { matches = YES; break; }
            }
            if (!matches) continue;
        }

        // Size filter at enumeration time so the live match count reflects
        // what the runner will actually process. Cheap attributesOfItem call.
        if (maxSizeBytes > 0) {
            NSDictionary *attrs = [fm attributesOfItemAtPath:full error:nil];
            NSNumber *sz = attrs[NSFileSize];
            if (sz && sz.longLongValue > maxSizeBytes) continue;
        }

        [out addObject:full];
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────

@interface NPPBatchDialog () <NSTextFieldDelegate>
@property (nonatomic, weak) MainWindowController *mwc;

// Configuration controls
@property (nonatomic, strong) NSPopUpButton *macroPopup;
@property (nonatomic, strong) NSTextField   *folderField;
@property (nonatomic, strong) NSButton      *browseBtn;
@property (nonatomic, strong) NSTextField   *extField;
@property (nonatomic, strong) NSButton      *recurseChk;
@property (nonatomic, strong) NSButton      *hiddenChk;
@property (nonatomic, strong) NSPopUpButton *sizeLimit;
@property (nonatomic, strong) NSButton      *saveAfterChk;
@property (nonatomic, strong) NSButton      *closeAfterChk;
@property (nonatomic, strong) NSButton      *closePreExistChk;
@property (nonatomic, strong) NSPopUpButton *errorPolicy;
@property (nonatomic, strong) NSTextField   *matchCountLbl;

// Progress controls (initially hidden)
@property (nonatomic, strong) NSProgressIndicator *progressBar;
@property (nonatomic, strong) NSTextField   *currentFileLbl;
@property (nonatomic, strong) NSTextField   *countsLbl;

// Action buttons
@property (nonatomic, strong) NSButton      *runBtn;
@property (nonatomic, strong) NSButton      *cancelBtn;

// State
@property (nonatomic, strong, nullable) NPPBatchRunner *runner;
@property (nonatomic, strong) NSArray<NSDictionary *> *cachedMacros;
/// When non-nil, the dialog is in fixed-file-list mode (Project Panel entry).
/// Folder enumeration is bypassed; this array IS the input file set, narrowed
/// only by the extension filter and size cap.
@property (nonatomic, strong, nullable) NSArray<NSString *> *fixedFileList;
@property (nonatomic, copy, nullable) NSString *fixedSourceDescription;
/// Extra label shown in files mode in place of the folder field.
@property (nonatomic, strong, nullable) NSTextField *sourceLabel;
@end

@implementation NPPBatchDialog

+ (void)presentForWindow:(MainWindowController *)mwc preselectedFolder:(NSString *)pre {
    NPPBatchDialog *d = [self new];
    d.mwc = mwc;
    [d _buildWindow];
    if (pre.length) d.folderField.stringValue = pre;
    [d _updateMatchCount];
    [d showWindow:nil];
    [d.window center];
    [d.window makeKeyAndOrderFront:nil];
    objc_setAssociatedObject(d.window, @selector(presentForWindow:preselectedFolder:),
                              d, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

+ (void)presentForWindow:(MainWindowController *)mwc
                    files:(NSArray<NSString *> *)files
                   source:(NSString *)sourceDescription {
    NPPBatchDialog *d = [self new];
    d.mwc = mwc;
    d.fixedFileList = files ?: @[];
    d.fixedSourceDescription = sourceDescription ?: @"";
    [d _buildWindow];
    [d _switchToFilesMode];
    [d _updateMatchCount];
    [d showWindow:nil];
    [d.window center];
    [d.window makeKeyAndOrderFront:nil];
    objc_setAssociatedObject(d.window, @selector(presentForWindow:preselectedFolder:),
                              d, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (void)_buildWindow {
    NppLocalizer *loc = [NppLocalizer shared];

    NSWindow *w = [[NSPanel alloc]
        initWithContentRect:NSMakeRect(0, 0, 520, 480)
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                    backing:NSBackingStoreBuffered defer:NO];
    w.title = [loc translate:@"Run Macro on Files"];
    w.releasedWhenClosed = NO;
    self.window = w;
    self.window.delegate = (id)self;

    NSView *cv = w.contentView;
    CGFloat W = 520, H = 480;

    // ─────── Layout starts from top, descending y ───────
    CGFloat y = H - 32;

    // Macro picker
    [cv addSubview:[self _label:[loc translate:@"Macro:"] x:20 y:y w:90 align:NSTextAlignmentRight]];
    _macroPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(115, y - 5, 380, 26)];
    [cv addSubview:_macroPopup];
    [self _populateMacroPopup];
    y -= 40;

    // Source folder
    [cv addSubview:[self _label:[loc translate:@"Folder:"] x:20 y:y w:90 align:NSTextAlignmentRight]];
    _folderField = [[NSTextField alloc] initWithFrame:NSMakeRect(115, y - 3, 285, 22)];
    _folderField.placeholderString = [loc translate:@"Pick a folder…"];
    _folderField.delegate = self;
    [cv addSubview:_folderField];
    _browseBtn = [NSButton buttonWithTitle:[loc translate:@"Browse…"]
                                    target:self action:@selector(_browse:)];
    _browseBtn.frame = NSMakeRect(405, y - 6, 95, 26);
    _browseBtn.bezelStyle = NSBezelStyleRounded;
    [cv addSubview:_browseBtn];
    y -= 40;

    // Extension filter
    [cv addSubview:[self _label:[loc translate:@"Extensions:"] x:20 y:y w:90 align:NSTextAlignmentRight]];
    _extField = [[NSTextField alloc] initWithFrame:NSMakeRect(115, y - 3, 380, 22)];
    _extField.stringValue = @"*.*";
    _extField.placeholderString = @"*.cpp; *.h; *.txt";
    _extField.delegate = self;
    [cv addSubview:_extField];
    y -= 32;

    // Recurse + Hidden
    _recurseChk = [NSButton checkboxWithTitle:[loc translate:@"Recurse subdirectories"]
                                       target:self action:@selector(_recurseChanged:)];
    _recurseChk.frame = NSMakeRect(115, y, 220, 18);
    _recurseChk.state = NSControlStateValueOn;
    [cv addSubview:_recurseChk];

    _hiddenChk = [NSButton checkboxWithTitle:[loc translate:@"Include hidden files"]
                                      target:self action:@selector(_recurseChanged:)];
    _hiddenChk.frame = NSMakeRect(335, y, 180, 18);
    [cv addSubview:_hiddenChk];
    y -= 30;

    // Size limit
    [cv addSubview:[self _label:[loc translate:@"Max size:"] x:20 y:y w:90 align:NSTextAlignmentRight]];
    _sizeLimit = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(115, y - 5, 120, 26)];
    NSArray *sizes = @[@"1 MB", @"10 MB", @"50 MB", @"200 MB", [loc translate:@"Unlimited"]];
    [_sizeLimit addItemsWithTitles:sizes];
    [_sizeLimit selectItemAtIndex:2];   // 50 MB
    _sizeLimit.target = self;
    _sizeLimit.action = @selector(_recurseChanged:);
    [cv addSubview:_sizeLimit];
    y -= 40;

    // Per-file options
    [cv addSubview:[self _label:[loc translate:@"Per file:"] x:20 y:y w:90 align:NSTextAlignmentRight]];
    _saveAfterChk = [NSButton checkboxWithTitle:[loc translate:@"Save if modified"]
                                         target:nil action:nil];
    _saveAfterChk.frame = NSMakeRect(115, y, 180, 18);
    _saveAfterChk.state = NSControlStateValueOn;
    [cv addSubview:_saveAfterChk];

    _closeAfterChk = [NSButton checkboxWithTitle:[loc translate:@"Close after"]
                                          target:self action:@selector(_closeAfterChanged:)];
    _closeAfterChk.frame = NSMakeRect(295, y, 130, 18);
    _closeAfterChk.state = NSControlStateValueOn;
    [cv addSubview:_closeAfterChk];
    y -= 22;

    _closePreExistChk = [NSButton checkboxWithTitle:[loc translate:@"Close even files that were already open"]
                                             target:nil action:nil];
    _closePreExistChk.frame = NSMakeRect(115, y, 360, 18);
    [cv addSubview:_closePreExistChk];
    y -= 32;

    // Error policy
    [cv addSubview:[self _label:[loc translate:@"On error:"] x:20 y:y w:90 align:NSTextAlignmentRight]];
    _errorPolicy = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(115, y - 5, 240, 26)];
    [_errorPolicy addItemsWithTitles:@[ [loc translate:@"Skip file and continue"],
                                        [loc translate:@"Stop on first error"] ]];
    [cv addSubview:_errorPolicy];
    y -= 36;

    // Match count
    _matchCountLbl = [NSTextField labelWithString:@""];
    _matchCountLbl.frame = NSMakeRect(115, y, 380, 18);
    _matchCountLbl.textColor = [NSColor secondaryLabelColor];
    [cv addSubview:_matchCountLbl];
    y -= 30;

    // ─────── Progress section (hidden until Run) ───────
    _progressBar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, y, W - 40, 16)];
    _progressBar.indeterminate = NO;
    _progressBar.minValue = 0;
    _progressBar.maxValue = 1;
    _progressBar.hidden = YES;
    [cv addSubview:_progressBar];
    y -= 26;

    _currentFileLbl = [NSTextField labelWithString:@""];
    _currentFileLbl.frame = NSMakeRect(20, y, W - 40, 16);
    _currentFileLbl.font = [NSFont systemFontOfSize:11];
    _currentFileLbl.textColor = [NSColor secondaryLabelColor];
    _currentFileLbl.lineBreakMode = NSLineBreakByTruncatingMiddle;
    _currentFileLbl.hidden = YES;
    [cv addSubview:_currentFileLbl];
    y -= 22;

    _countsLbl = [NSTextField labelWithString:@""];
    _countsLbl.frame = NSMakeRect(20, y, W - 40, 16);
    _countsLbl.font = [NSFont systemFontOfSize:11];
    _countsLbl.textColor = [NSColor secondaryLabelColor];
    _countsLbl.hidden = YES;
    [cv addSubview:_countsLbl];

    // ─────── Buttons ───────
    _cancelBtn = [NSButton buttonWithTitle:[loc translate:@"Cancel"]
                                    target:self action:@selector(_cancel:)];
    _cancelBtn.frame = NSMakeRect(W - 100 - 100 - 20, 16, 100, 32);
    _cancelBtn.bezelStyle = NSBezelStyleRounded;
    _cancelBtn.keyEquivalent = @"\033";
    [cv addSubview:_cancelBtn];

    _runBtn = [NSButton buttonWithTitle:[loc translate:@"Run"]
                                 target:self action:@selector(_run:)];
    _runBtn.frame = NSMakeRect(W - 100 - 20, 16, 100, 32);
    _runBtn.bezelStyle = NSBezelStyleRounded;
    _runBtn.keyEquivalent = @"\r";
    [cv addSubview:_runBtn];
}

- (NSTextField *)_label:(NSString *)s x:(CGFloat)x y:(CGFloat)y w:(CGFloat)w align:(NSTextAlignment)a {
    NSTextField *t = [NSTextField labelWithString:s];
    t.frame = NSMakeRect(x, y, w, 18);
    t.alignment = a;
    return t;
}

// Reshape the dialog for fixed-file-list mode: hide folder field + Browse +
// recurse / hidden checkboxes (they don't apply to a flat list), and show
// a read-only label with the source description where the folder used to be.
- (void)_switchToFilesMode {
    _folderField.hidden = YES;
    _browseBtn.hidden   = YES;
    _recurseChk.hidden  = YES;
    _hiddenChk.hidden   = YES;

    _sourceLabel = [NSTextField labelWithString:_fixedSourceDescription ?: @""];
    _sourceLabel.frame = _folderField.frame;   // same slot
    _sourceLabel.textColor = [NSColor labelColor];
    _sourceLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self.window.contentView addSubview:_sourceLabel];
}

- (void)_populateMacroPopup {
    _cachedMacros = loadMacrosFromShortcutsXML();
    [_macroPopup removeAllItems];
    if (_cachedMacros.count == 0) {
        [_macroPopup addItemWithTitle:[[NppLocalizer shared] translate:@"(no saved macros)"]];
        _macroPopup.enabled = NO;
        return;
    }
    for (NSDictionary *m in _cachedMacros) {
        [_macroPopup addItemWithTitle:m[@"name"] ?: @"Untitled"];
    }
    _macroPopup.enabled = YES;
}

#pragma mark - User actions

- (void)_browse:(id)sender {
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.canChooseFiles = NO;
    p.canChooseDirectories = YES;
    p.allowsMultipleSelection = NO;
    NSString *cur = _folderField.stringValue;
    if (cur.length && [[NSFileManager defaultManager] fileExistsAtPath:cur]) {
        p.directoryURL = [NSURL fileURLWithPath:cur];
    }
    if ([p runModal] == NSModalResponseOK) {
        _folderField.stringValue = p.URL.path;
        [self _updateMatchCount];
    }
}

// One handler for every control that changes the enumerated file set. Cheap
// to re-enumerate up to the visible cap; if it ever becomes slow we'll
// debounce on a perform:afterDelay:.
- (void)_recurseChanged:(id)sender {
    [self _updateMatchCount];
}

- (void)_closeAfterChanged:(id)sender {
    _closePreExistChk.enabled = (_closeAfterChk.state == NSControlStateValueOn);
}

- (void)controlTextDidChange:(NSNotification *)note {
    [self _updateMatchCount];
}

- (NSArray<NSString *> *)_currentFilteredFileList {
    NSArray *globs = [_extField.stringValue componentsSeparatedByCharactersInSet:
        [NSCharacterSet characterSetWithCharactersInString:@",; "]];
    NSInteger maxSize = [self _selectedMaxSizeBytes];

    // Files mode (Project Panel): the list is already known, just filter it.
    if (_fixedFileList) {
        return filterFiles(_fixedFileList, globs, maxSize);
    }

    // Folder mode: walk the filesystem.
    NSString *folder = _folderField.stringValue;
    if (!folder.length || ![[NSFileManager defaultManager] fileExistsAtPath:folder]) {
        return @[];
    }
    return enumerateFiles(folder, globs,
                          _recurseChk.state == NSControlStateValueOn,
                          _hiddenChk.state == NSControlStateValueOn,
                          maxSize);
}

- (void)_updateMatchCount {
    NSArray<NSString *> *files = [self _currentFilteredFileList];
    _matchCountLbl.stringValue = [NSString stringWithFormat:
        [[NppLocalizer shared] translate:@"%lu file(s) match"], (unsigned long)files.count];
}

- (NSInteger)_selectedMaxSizeBytes {
    switch (_sizeLimit.indexOfSelectedItem) {
        case 0: return 1   * 1024 * 1024;
        case 1: return 10  * 1024 * 1024;
        case 2: return 50  * 1024 * 1024;
        case 3: return 200 * 1024 * 1024;
        default: return 0;   // Unlimited
    }
}

- (void)_cancel:(id)sender {
    if (_runner) {
        // Mid-run: request cancellation; the runner finishes the current file
        // and exits. UI will update from the completion callback.
        [_runner cancel];
        return;
    }
    [self.window close];
}

- (void)_run:(id)sender {
    if (_runner) return;   // already running

    if (_cachedMacros.count == 0) { NSBeep(); return; }

    NSInteger macroIdx = _macroPopup.indexOfSelectedItem;
    if (macroIdx < 0 || macroIdx >= (NSInteger)_cachedMacros.count) { NSBeep(); return; }
    NSDictionary *macro = _cachedMacros[macroIdx];
    NSArray<NSDictionary *> *actions = macro[@"actions"];
    if (!actions.count) { NSBeep(); return; }

    // Folder mode requires a real folder path; files mode skips that check.
    if (!_fixedFileList) {
        NSString *folder = _folderField.stringValue;
        if (!folder.length ||
            ![[NSFileManager defaultManager] fileExistsAtPath:folder]) {
            NSAlert *a = [NSAlert new];
            a.messageText = [[NppLocalizer shared] translate:@"Folder not found"];
            a.informativeText = [[NppLocalizer shared] translate:@"Pick a folder that exists."];
            [a runModal];
            return;
        }
    }

    NSArray<NSString *> *files = [self _currentFilteredFileList];
    if (files.count == 0) {
        NSAlert *a = [NSAlert new];
        a.messageText = [[NppLocalizer shared] translate:@"No files match"];
        a.informativeText = [[NppLocalizer shared] translate:@"Adjust the extensions filter, recursion, or hidden-files option."];
        [a runModal];
        return;
    }

    NPPBatchOptions *opts = [NPPBatchOptions new];
    opts.filePaths            = files;
    opts.macroActions         = actions;
    opts.saveAfter            = (_saveAfterChk.state == NSControlStateValueOn);
    opts.closeAfter           = (_closeAfterChk.state == NSControlStateValueOn);
    opts.closePreExistingTabs = (_closePreExistChk.state == NSControlStateValueOn);
    opts.maxFileSizeBytes     = [self _selectedMaxSizeBytes];
    opts.errorPolicy          = (NPPBatchErrorPolicy)_errorPolicy.indexOfSelectedItem;

    [self _enterProgressState];

    _runner = [NPPBatchRunner runnerForWindow:_mwc options:opts];
    __weak typeof(self) weakSelf = self;
    [_runner runWithProgress:^(NSInteger done, NSInteger total, NSString *path,
                               NSInteger ok, NSInteger skipped, NSInteger failed) {
        [weakSelf _updateProgress:done total:total path:path
                              ok:ok skipped:skipped failed:failed];
    } completion:^(NSArray<NPPBatchRunResult *> *results, BOOL cancelled) {
        [weakSelf _showResults:results cancelled:cancelled];
    }];
}

- (void)_enterProgressState {
    NppLocalizer *loc = [NppLocalizer shared];
    NSArray *toDisable = @[ _macroPopup, _folderField, _browseBtn, _extField,
                            _recurseChk, _hiddenChk, _sizeLimit,
                            _saveAfterChk, _closeAfterChk, _closePreExistChk,
                            _errorPolicy, _runBtn ];
    for (id ctrl in toDisable) [ctrl setEnabled:NO];

    _progressBar.hidden = NO;
    _currentFileLbl.hidden = NO;
    _countsLbl.hidden = NO;
    _cancelBtn.title = [loc translate:@"Cancel"];
    _currentFileLbl.stringValue = [loc translate:@"Starting…"];
    _countsLbl.stringValue = @"";
    [_progressBar startAnimation:nil];
}

- (void)_updateProgress:(NSInteger)done
                  total:(NSInteger)total
                   path:(NSString *)path
                     ok:(NSInteger)ok
                skipped:(NSInteger)skipped
                 failed:(NSInteger)failed {
    _progressBar.maxValue = (double)total;
    _progressBar.doubleValue = (double)done;
    _currentFileLbl.stringValue = [NSString stringWithFormat:@"%@ %@",
        [[NppLocalizer shared] translate:@"Processing:"], path.lastPathComponent];
    _countsLbl.stringValue = [NSString stringWithFormat:
        [[NppLocalizer shared] translate:@"%ld / %ld   Ok: %ld   Skipped: %ld   Failed: %ld"],
        (long)done, (long)total, (long)ok, (long)skipped, (long)failed];
}

- (void)_showResults:(NSArray<NPPBatchRunResult *> *)results cancelled:(BOOL)cancelled {
    _runner = nil;
    [_progressBar stopAnimation:nil];

    NSInteger ok = 0, skipped = 0, failed = 0;
    for (NPPBatchRunResult *r in results) {
        switch (r.outcome) {
            case NPPBatchOutcomeOk:                  ok++; break;
            case NPPBatchOutcomeSkippedTooBig:
            case NPPBatchOutcomeSkippedPreExisting:  skipped++; break;
            case NPPBatchOutcomeFailedToOpen:
            case NPPBatchOutcomeMacroFailed:
            case NPPBatchOutcomeSaveFailed:          failed++; break;
            case NPPBatchOutcomeCancelled:           break;
        }
    }

    NppLocalizer *loc = [NppLocalizer shared];
    NSAlert *a = [NSAlert new];
    a.messageText = cancelled
        ? [loc translate:@"Batch cancelled"]
        : [loc translate:@"Batch complete"];
    a.informativeText = [NSString stringWithFormat:
        [loc translate:@"Total: %lu   Ok: %ld   Skipped: %ld   Failed: %ld"],
        (unsigned long)results.count, (long)ok, (long)skipped, (long)failed];
    [a addButtonWithTitle:[loc translate:@"OK"]];

    if (failed > 0 || skipped > 0) {
        // Aggregate failed/skipped reasons into a short scrollable accessory.
        NSScrollView *sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 460, 180)];
        sv.hasVerticalScroller = YES;
        NSTextView *tv = [[NSTextView alloc] initWithFrame:sv.bounds];
        tv.editable = NO;
        tv.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        NSMutableString *buf = [NSMutableString string];
        for (NPPBatchRunResult *r in results) {
            if (r.outcome == NPPBatchOutcomeOk) continue;
            NSString *tag = @"?";
            switch (r.outcome) {
                case NPPBatchOutcomeFailedToOpen:       tag = @"open"; break;
                case NPPBatchOutcomeMacroFailed:        tag = @"macro"; break;
                case NPPBatchOutcomeSaveFailed:         tag = @"save"; break;
                case NPPBatchOutcomeSkippedTooBig:      tag = @"too big"; break;
                case NPPBatchOutcomeSkippedPreExisting: tag = @"pre-open"; break;
                case NPPBatchOutcomeCancelled:          tag = @"cancel"; break;
                case NPPBatchOutcomeOk: break;
            }
            [buf appendFormat:@"[%@] %@\n", tag, r.filePath];
            if (r.errorMessage.length) [buf appendFormat:@"        %@\n", r.errorMessage];
        }
        tv.string = buf;
        sv.documentView = tv;
        a.accessoryView = sv;
    }

    [self _exitProgressState];
    [a runModal];
    [self.window close];
}

- (void)_exitProgressState {
    _progressBar.hidden = YES;
    _currentFileLbl.hidden = YES;
    _countsLbl.hidden = YES;
    NSArray *toEnable = @[ _macroPopup, _folderField, _browseBtn, _extField,
                           _recurseChk, _hiddenChk, _sizeLimit,
                           _saveAfterChk, _closeAfterChk, _closePreExistChk,
                           _errorPolicy, _runBtn ];
    for (id ctrl in toEnable) [ctrl setEnabled:YES];
    _closePreExistChk.enabled = (_closeAfterChk.state == NSControlStateValueOn);
}

#pragma mark - NSWindowDelegate

- (void)windowWillClose:(NSNotification *)note {
    // Detach the associated reference so we get released.
    if (_runner) [_runner cancel];
    objc_setAssociatedObject(self.window, @selector(presentForWindow:preselectedFolder:),
                              nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

@end
