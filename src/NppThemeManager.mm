#import "NppThemeManager.h"

NSNotificationName const NPPDarkModeChangedNotification = @"NPPDarkModeChangedNotification";
NSString *const kPrefDarkMode = @"NPPDarkMode";

// ── Toolbar icon name mapping: standard name → Fluent name ──────────────────
// To remap an icon: change the right-hand value only.
// Light icons live in icons/light/toolbar/regular/{fluentName}.png
// Dark icons live in icons/dark/toolbar/regular/{fluentName}.png
static NSDictionary<NSString *, NSString *> *toolbarIconMapping(void) {
    static NSDictionary *map;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        map = @{
            @"newFile":       @"new_off",
            @"openFile":      @"open_off",
            @"saveFile":      @"save_off",
            @"saveAll":       @"saveall_off",
            @"closeFile":     @"close_off",
            @"closeAll":      @"closeall_off",
            @"print":         @"print_off",
            @"cut":           @"cut_off",
            @"copy":          @"copy_off",
            @"paste":         @"paste_off",
            @"undo":          @"undo_off",
            @"redo":          @"redo_off",
            @"find":          @"find_off",
            @"findReplace":   @"findrep_off",
            @"zoomIn":        @"zoomIn_off",
            @"zoomOut":       @"zoomOut_off",
            @"syncV":         @"syncV_off",
            @"syncH":         @"syncH_off",
            @"wrap":          @"wrap_off",
            @"allChars":      @"allChars_off",
            @"indentGuide":   @"indentGuide_off",
            @"udl":           @"udl_off",
            @"docMap":        @"docMap_off",
            @"docList":       @"docList_off",
            @"funcList":      @"funcList_off",
            @"fileBrowser":   @"fileBrowser_off",
            @"monitoring":    @"monitoring_off",
            @"startRecord":   @"startrecord_off",
            @"stopRecord":    @"stoprecord_off",
            @"playRecord":    @"playrecord_off",
            @"playRecord_m":  @"playrecord_m_off",
            @"saveRecord":    @"saverecord_off",
            // Red save icon (unsaved tab indicator) — no dark variant, use same
            @"saveFileRed":   @"save_off",
        };
    });
    return map;
}

@implementation NppThemeManager {
    BOOL _cachedIsDark;
}

+ (instancetype)shared {
    static NppThemeManager *inst;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ inst = [[NppThemeManager alloc] init]; });
    return inst;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        // Load saved preference (default = Auto)
        NSInteger saved = [[NSUserDefaults standardUserDefaults] integerForKey:kPrefDarkMode];
        _mode = (NppDarkModeOption)saved;
        [self _recalcIsDark];
        [self _applyAppearance];

        // Observe system appearance changes for Auto mode via distributed notification
        [[NSDistributedNotificationCenter defaultCenter]
            addObserver:self selector:@selector(_systemAppearanceDidChange:)
                   name:@"AppleInterfaceThemeChangedNotification" object:nil];
    }
    return self;
}

- (void)_systemAppearanceDidChange:(NSNotification *)n {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self _systemAppearanceChanged];
    });
}

- (void)_systemAppearanceChanged {
    if (_mode == NppDarkModeAuto) {
        BOOL wasDark = _cachedIsDark;
        [self _recalcIsDark];
        if (_cachedIsDark != wasDark) {
            [self _applyAppearance];
            [[NSNotificationCenter defaultCenter]
                postNotificationName:NPPDarkModeChangedNotification object:nil];
        }
    }
}

- (void)_recalcIsDark {
    switch (_mode) {
        case NppDarkModeLight: _cachedIsDark = NO; break;
        case NppDarkModeDark:  _cachedIsDark = YES; break;
        case NppDarkModeAuto:
        default: {
            NSAppearanceName name = [NSApp.effectiveAppearance
                bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
            _cachedIsDark = [name isEqualToString:NSAppearanceNameDarkAqua];
            break;
        }
    }
}

- (void)setMode:(NppDarkModeOption)mode {
    _mode = mode;
    [[NSUserDefaults standardUserDefaults] setInteger:mode forKey:kPrefDarkMode];
    [self _recalcIsDark];
    [self _applyAppearance];
    [[NSNotificationCenter defaultCenter]
        postNotificationName:NPPDarkModeChangedNotification object:nil];
}

/// Set NSApp.appearance to force all native controls (toolbar, title bar,
/// scrollbars, dialogs, buttons) to render in the correct mode.
- (void)_applyAppearance {
    if (_mode == NppDarkModeAuto) {
        // nil = follow system (default macOS behavior)
        NSApp.appearance = nil;
    } else {
        NSApp.appearance = [NSAppearance appearanceNamed:
            _cachedIsDark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    }
}

- (BOOL)isDark { return _cachedIsDark; }

// ── Tab Bar Colors ───────────────────────────────────────────────────────────

- (NSColor *)tabBarBackground {
    return _cachedIsDark ? [NSColor colorWithWhite:0.18 alpha:1]
                         : [NSColor colorWithRed:0xF0/255.0 green:0xF0/255.0 blue:0xF0/255.0 alpha:1];
}

- (NSColor *)activeTabFill {
    return _cachedIsDark ? [NSColor colorWithWhite:0.25 alpha:1]
                         : [NSColor colorWithWhite:1.0 alpha:1];
}

- (NSColor *)inactiveTabFill {
    return _cachedIsDark ? [NSColor colorWithWhite:0.20 alpha:1]
                         : [NSColor colorWithWhite:0.80 alpha:1];
}

- (NSColor *)hoverTabFill {
    return _cachedIsDark ? [NSColor colorWithWhite:0.28 alpha:1]
                         : [NSColor colorWithWhite:0.87 alpha:1];
}

- (NSColor *)inactiveTabGradientTop {
    return _cachedIsDark ? [NSColor colorWithWhite:0.22 alpha:1]
                         : [NSColor colorWithWhite:0.86 alpha:1];
}

- (NSColor *)inactiveTabGradientBottom {
    return _cachedIsDark ? [NSColor colorWithWhite:0.20 alpha:1]
                         : [NSColor colorWithWhite:0.80 alpha:1];
}

- (NSColor *)hoverTabGradientTop {
    return _cachedIsDark ? [NSColor colorWithWhite:0.30 alpha:1]
                         : [NSColor colorWithWhite:0.90 alpha:1];
}

- (NSColor *)hoverTabGradientBottom {
    return _cachedIsDark ? [NSColor colorWithWhite:0.28 alpha:1]
                         : [NSColor colorWithWhite:0.87 alpha:1];
}

- (NSColor *)accentStripe {
    // Same orange accent in both modes
    return [NSColor colorWithRed:253/255.0 green:166/255.0 blue:64/255.0 alpha:1];
}

- (NSColor *)tabBorder {
    return _cachedIsDark ? [NSColor colorWithWhite:0.35 alpha:1]
                         : [NSColor colorWithWhite:0.58 alpha:1];
}

- (NSColor *)tabText {
    return _cachedIsDark ? [NSColor colorWithWhite:0.90 alpha:1]
                         : [NSColor labelColor];
}

- (NSColor *)tabTextInactive {
    return _cachedIsDark ? [NSColor colorWithWhite:0.65 alpha:1]
                         : [NSColor colorWithWhite:0.15 alpha:1];
}

- (NSColor *)dividerDark {
    return _cachedIsDark ? [NSColor colorWithWhite:0.30 alpha:1]
                         : [NSColor colorWithWhite:0.55 alpha:1];
}

- (NSColor *)dividerLight {
    return _cachedIsDark ? [NSColor colorWithWhite:0.22 alpha:1]
                         : [NSColor colorWithWhite:0.96 alpha:1];
}

// ── Scroll Arrow Colors ──────────────────────────────────────────────────────

- (NSColor *)arrowHoverBg {
    return _cachedIsDark ? [NSColor colorWithWhite:0.30 alpha:1]
                         : [NSColor colorWithWhite:0.91 alpha:1];
}

- (NSColor *)arrowPressBg {
    return _cachedIsDark ? [NSColor colorWithWhite:0.35 alpha:1]
                         : [NSColor colorWithWhite:0.83 alpha:1];
}

- (NSColor *)arrowBorder {
    return _cachedIsDark ? [NSColor colorWithWhite:0.35 alpha:1]
                         : [NSColor colorWithWhite:0.50 alpha:1];
}

- (NSColor *)arrowFill {
    return _cachedIsDark ? [NSColor colorWithWhite:0.75 alpha:1]
                         : [NSColor colorWithWhite:0.18 alpha:1];
}

// ── Panel / Status Bar ───────────────────────────────────────────────────────

- (NSColor *)panelBackground {
    return _cachedIsDark ? [NSColor colorWithWhite:0.18 alpha:1]
                         : [NSColor controlBackgroundColor];
}

- (NSColor *)statusBarBackground {
    return _cachedIsDark ? [NSColor colorWithWhite:0.18 alpha:1]
                         : [NSColor windowBackgroundColor];
}

// ── Icon Paths ───────────────────────────────────────────────────────────────

- (NSString *)toolbarIconDir {
    return _cachedIsDark ? @"icons/dark/toolbar/regular" : @"icons/light/toolbar/regular";
}

- (NSString *)tabbarIconDir {
    return _cachedIsDark ? @"icons/dark/tabbar" : @"icons/standard/tabbar";
}

- (NSString *)panelsIconDir {
    return _cachedIsDark ? @"icons/dark/panels" : @"icons/standard/panels";
}

- (nullable NSImage *)toolbarIconNamed:(NSString *)standardName {
    NSString *dir = self.toolbarIconDir;

    // Both light and dark dirs use Fluent naming — always map.
    NSString *fileName = toolbarIconMapping()[standardName];
    if (!fileName) fileName = standardName;

    NSString *path = [[NSBundle mainBundle] pathForResource:fileName ofType:@"png"
                                               inDirectory:dir];
    return path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
}

- (nullable NSImage *)tabbarIconNamed:(NSString *)name {
    NSString *path = [[NSBundle mainBundle] pathForResource:name ofType:@"png"
                                               inDirectory:self.tabbarIconDir];
    return path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
}

@end
