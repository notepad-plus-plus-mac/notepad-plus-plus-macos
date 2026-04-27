#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

/// Posted when dark mode state changes. All UI components should re-query colors.
extern NSNotificationName const NPPDarkModeChangedNotification;

/// Preference key for dark mode setting (Auto/Light/Dark).
extern NSString *const kPrefDarkMode;

/// Posted when toolbar icon style changes. MainWindowController reloads icons.
extern NSNotificationName const NPPToolbarIconStyleChangedNotification;

/// Preference key for toolbar icon style (Fluent/Classic).
extern NSString *const kPrefToolbarIconStyle;

typedef NS_ENUM(NSInteger, NppDarkModeOption) {
    NppDarkModeAuto  = 0,  // Follow macOS system appearance
    NppDarkModeLight = 1,  // Always light
    NppDarkModeDark  = 2,  // Always dark
};

typedef NS_ENUM(NSInteger, NppToolbarIconStyle) {
    NppToolbarIconStyleFluent  = 0,  // Modern Fluent icons (96x96, light/dark variants)
    NppToolbarIconStyleClassic = 1,  // Original Windows-style pixel-art icons (32x32)
};

/// Centralized theme manager. All UI components query this for colors and icon paths.
/// Never hardcode colors — always go through NppThemeManager.
@interface NppThemeManager : NSObject

+ (instancetype)shared;

/// Current mode (Auto/Light/Dark). Setting this posts NPPDarkModeChangedNotification.
@property (nonatomic) NppDarkModeOption mode;

/// YES if the effective appearance is currently dark.
@property (nonatomic, readonly) BOOL isDark;

/// Toolbar icon style (Fluent/Classic). Setting this posts NPPToolbarIconStyleChangedNotification.
@property (nonatomic) NppToolbarIconStyle toolbarIconStyle;

// ── UI Colors ────────────────────────────────────────────────────────────────

// Tab bar
@property (nonatomic, readonly) NSColor *tabBarBackground;
@property (nonatomic, readonly) NSColor *activeTabFill;
@property (nonatomic, readonly) NSColor *inactiveTabFill;
@property (nonatomic, readonly) NSColor *hoverTabFill;
@property (nonatomic, readonly) NSColor *inactiveTabGradientTop;
@property (nonatomic, readonly) NSColor *inactiveTabGradientBottom;
@property (nonatomic, readonly) NSColor *hoverTabGradientTop;
@property (nonatomic, readonly) NSColor *hoverTabGradientBottom;
@property (nonatomic, readonly) NSColor *accentStripe;
@property (nonatomic, readonly) NSColor *tabBorder;
@property (nonatomic, readonly) NSColor *tabText;
@property (nonatomic, readonly) NSColor *tabTextInactive;
@property (nonatomic, readonly) NSColor *dividerDark;
@property (nonatomic, readonly) NSColor *dividerLight;

// Tab scroll arrows
@property (nonatomic, readonly) NSColor *arrowHoverBg;
@property (nonatomic, readonly) NSColor *arrowPressBg;
@property (nonatomic, readonly) NSColor *arrowBorder;
@property (nonatomic, readonly) NSColor *arrowFill;

// Panels / status bar
@property (nonatomic, readonly) NSColor *panelBackground;
@property (nonatomic, readonly) NSColor *statusBarBackground;

// ── Icon Paths ───────────────────────────────────────────────────────────────

/// Returns the toolbar icon directory based on dark mode and icon style.
@property (nonatomic, readonly) NSString *toolbarIconDir;

/// Returns the tabbar icon directory: "icons/standard/tabbar" or "icons/dark/tabbar"
@property (nonatomic, readonly) NSString *tabbarIconDir;

/// Returns the panels icon base directory: "icons/standard/panels" or "icons/dark/panels"
@property (nonatomic, readonly) NSString *panelsIconDir;

/// Load a toolbar icon by standard name (e.g. "saveFile"). Automatically maps to
/// dark icon name if in dark mode. Returns nil if not found.
- (nullable NSImage *)toolbarIconNamed:(NSString *)standardName;

/// Load a tabbar icon by name (e.g. "closeTabButton"). Uses current theme directory.
- (nullable NSImage *)tabbarIconNamed:(NSString *)name;

@end

NS_ASSUME_NONNULL_END
