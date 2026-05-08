#import "NppTabBar.h"
#import "NppThemeManager.h"
#import "PreferencesWindowController.h"

@interface NppTabBar (ContextMenu)
- (NSMenu *)buildTabContextMenu;
@end

// ── Constants ─────────────────────────────────────────────────────────────────
// Bar layout: baseBarH = kTabTopGap + inactiveTabH + 1(border).
// inactiveTabH = barH - kTabTopGap - 1.  activeTabH = inactiveTabH + kActiveBoost.
// In wrap mode the view reports a taller intrinsic height as rows are added.
static const CGFloat kTabBarBaseHeight = 25.0;
static const CGFloat kTabTopGap    = 5.0;   // dead space at bar top (gap below toolbar)
static const CGFloat kActiveBoost  = 3.0;   // px active tab is taller than inactive
static const CGFloat kTabMinWidth  = 80.0;
static const CGFloat kTabMaxWidth  = 190.0;
static const CGFloat kIconSize     = 16.0;
static const CGFloat kCloseSize    = 14.0;
static const CGFloat kArrowBtnW    = 14.0;  // width of each scroll-arrow button

// ── Colors (all routed through NppThemeManager) ──────────────────────────────
#define TM [NppThemeManager shared]
static NSColor *tabBarBgColor()    { return TM.tabBarBackground; }
static NSColor *activeTabColor()   { return TM.activeTabFill; }
static NSColor *accentColor()      { return TM.accentStripe; }
// Per-tab color palette (same for light/dark)
static NSColor *tabColorForId(NSInteger colorId) {
    switch (colorId) {
        case 0: return [NSColor colorWithRed:0xFC/255.0 green:0xE3/255.0 blue:0x86/255.0 alpha:1]; // Yellow
        case 1: return [NSColor colorWithRed:0xA9/255.0 green:0xF0/255.0 blue:0x8C/255.0 alpha:1]; // Green
        case 2: return [NSColor colorWithRed:0x7A/255.0 green:0xC9/255.0 blue:0xF5/255.0 alpha:1]; // Blue
        case 3: return [NSColor colorWithRed:0xF5/255.0 green:0xB6/255.0 blue:0x7A/255.0 alpha:1]; // Orange
        case 4: return [NSColor colorWithRed:0xF0/255.0 green:0x8C/255.0 blue:0xF0/255.0 alpha:1]; // Pink
        default: return nil; // -1 = use default accent
    }
}
static NSColor *tabBorderColor()   { return TM.tabBorder; }
static NSColor *dividerGray()      { return TM.dividerDark; }
static NSColor *dividerWhite()     { return TM.dividerLight; }

// ── Icon helpers (routed through NppThemeManager) ────────────────────────────
static NSImage *tabIcon(NSString *name) {
    return [TM tabbarIconNamed:name];
}
static NSImage *toolbarIcon(NSString *name) {
    return [TM toolbarIconNamed:name];
}

// ── Windows-style scroll arrow button ────────────────────────────────────────
@interface _NppScrollArrowButton : NSButton {
    BOOL _pointsRight;
    BOOL _hovering;
}
- (instancetype)initPointingRight:(BOOL)right target:(id)tgt action:(SEL)act;
@end

@implementation _NppScrollArrowButton

- (instancetype)initPointingRight:(BOOL)right target:(id)tgt action:(SEL)act {
    self = [super init];
    if (self) {
        _pointsRight = right;
        [self setBordered:NO];
        self.buttonType = NSButtonTypeMomentaryChange;
        self.title  = @"";
        self.target = tgt;
        self.action = act;
        self.hidden = YES;
        NSTrackingArea *ta = [[NSTrackingArea alloc]
            initWithRect:NSZeroRect
                 options:(NSTrackingMouseEnteredAndExited |
                          NSTrackingActiveInActiveApp     |
                          NSTrackingInVisibleRect)
                   owner:self userInfo:nil];
        [self addTrackingArea:ta];
    }
    return self;
}

- (void)mouseEntered:(NSEvent *)e { _hovering = YES;  [self setNeedsDisplay:YES]; }
- (void)mouseExited:(NSEvent *)e  { _hovering = NO;   [self setNeedsDisplay:YES]; }

- (void)drawRect:(NSRect)dirtyRect {
    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;

    // Background — slightly brighter on hover
    NSColor *bg = _hovering ? TM.arrowHoverBg : TM.arrowPressBg;
    [bg setFill];
    NSRectFill(self.bounds);

    // 1px border
    [TM.arrowBorder setStroke];
    NSBezierPath *border = [NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds, 0.5, 0.5)];
    border.lineWidth = 0.5;
    [border stroke];

    // Small solid triangle centered in the button
    CGFloat aw = 4.0, ah = 7.0;
    CGFloat ax = floor((w - aw) / 2.0);
    CGFloat ay = floor((h - ah) / 2.0);

    NSBezierPath *tri = [NSBezierPath bezierPath];
    if (_pointsRight) {
        [tri moveToPoint:NSMakePoint(ax,      ay)];
        [tri lineToPoint:NSMakePoint(ax + aw, ay + ah / 2.0)];
        [tri lineToPoint:NSMakePoint(ax,      ay + ah)];
    } else {
        [tri moveToPoint:NSMakePoint(ax + aw, ay)];
        [tri lineToPoint:NSMakePoint(ax,      ay + ah / 2.0)];
        [tri lineToPoint:NSMakePoint(ax + aw, ay + ah)];
    }
    [tri closePath];
    [TM.arrowFill setFill];
    [tri fill];
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - _NppTabItem (private)

@interface _NppTabItem : NSView {
    BOOL _hovered;
    BOOL _closeHovered;
    NSTrackingArea *_trackingArea;
}
@property (nonatomic) NSInteger tabIndex;
@property (nonatomic, copy) NSString *title;
@property (nonatomic) BOOL isSelected;
@property (nonatomic) BOOL isModified;
@property (nonatomic) BOOL isPinned;
@property (nonatomic) NSInteger colorId;  // -1 = default orange, 0–4 = color 1–5
@property (nonatomic, weak) id target;
@property (nonatomic) SEL selectAction;
@property (nonatomic) SEL closeAction;
- (CGFloat)preferredWidth;
@end

@implementation _NppTabItem

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) { self.wantsLayer = YES; _colorId = -1; }
    return self;
}

static const CGFloat kPinSize = 11.0; // pin icon drawn at ~80% of original ~14px

- (CGFloat)preferredWidth {
    NSDictionary *attrs = @{NSFontAttributeName: [NSFont systemFontOfSize:11 weight:NSFontWeightRegular]};
    CGFloat tw       = [_title sizeWithAttributes:attrs].width;
    CGFloat closeGap = 4 + kCloseSize + 4;
    CGFloat pinGap   = _isPinned ? (kPinSize + 2) : 0; // pin icon to left of close
    NSInteger maxW = [[NSUserDefaults standardUserDefaults] integerForKey:kPrefTabMaxLabelWidth];
    if (maxW < 80) maxW = 80;
    BOOL showClose = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefTabCloseButton];
    if (!showClose) closeGap = 0;
    return MAX(kTabMinWidth, MIN((CGFloat)maxW, 8 + kIconSize + 4 + tw + pinGap + closeGap + 8));
}

- (void)drawRect:(NSRect)dirtyRect {
    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;
    CGFloat r = 2.0;

    // ── Tab shape: rounded top corners, flat bottom ───────────────────────────
    NSBezierPath *tabPath = [NSBezierPath bezierPath];
    [tabPath moveToPoint:NSMakePoint(0, 0)];
    [tabPath lineToPoint:NSMakePoint(0, h - r)];
    [tabPath appendBezierPathWithArcWithCenter:NSMakePoint(r, h - r)
                                         radius:r startAngle:180 endAngle:90 clockwise:YES];
    [tabPath lineToPoint:NSMakePoint(w - r, h)];
    [tabPath appendBezierPathWithArcWithCenter:NSMakePoint(w - r, h - r)
                                         radius:r startAngle:90 endAngle:0 clockwise:YES];
    [tabPath lineToPoint:NSMakePoint(w, 0)];
    [tabPath closePath];

    // ── Fill ──────────────────────────────────────────────────────────────────
    if (_isSelected) {
        [activeTabColor() setFill];
        [tabPath fill];
    } else {
        NSColor *top    = _hovered ? TM.hoverTabGradientTop    : TM.inactiveTabGradientTop;
        NSColor *bottom = _hovered ? TM.hoverTabGradientBottom : TM.inactiveTabGradientBottom;
        NSGradient *g = [[NSGradient alloc] initWithStartingColor:top endingColor:bottom];
        [NSGraphicsContext saveGraphicsState];
        [tabPath addClip];
        [g drawInRect:self.bounds angle:270];
        [NSGraphicsContext restoreGraphicsState];
    }

    // ── Border ────────────────────────────────────────────────────────────────
    [tabBorderColor() setStroke];
    tabPath.lineWidth = 0.5;
    [tabPath stroke];

    // ── Accent stripe: 3px at top, clipped to tab shape ─────────────────────
    // Active tab always shows a stripe (per-tab color or default orange).
    // Inactive tabs with a color assigned also show the stripe.
    NSColor *stripe = tabColorForId(_colorId) ?: accentColor();
    if (_isSelected || _colorId >= 0) {
        [NSGraphicsContext saveGraphicsState];
        [tabPath addClip];
        [stripe setFill];
        NSRectFill(NSMakeRect(0, h - 3, w, 3));
        [NSGraphicsContext restoreGraphicsState];
    }

    // ── Floppy icon ───────────────────────────────────────────────────────────
    NSImage *icon = _isModified ? toolbarIcon(@"saveFileRed") : toolbarIcon(@"saveFile");
    if (icon) {
        CGFloat sz  = kIconSize * 0.704;
        NSRect  ir  = NSMakeRect(8 + (kIconSize - sz) / 2.0, (h - sz) / 2.0, sz, sz);
        [icon drawInRect:ir fromRect:NSZeroRect
               operation:NSCompositingOperationSourceOver fraction:1.0];
    }

    // ── Title ─────────────────────────────────────────────────────────────────
    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    ps.lineBreakMode = NSLineBreakByTruncatingMiddle;
    NSColor *textColor = _isSelected ? TM.tabText : TM.tabTextInactive;
    NSFont  *font      = _isSelected ? [NSFont systemFontOfSize:11 weight:NSFontWeightMedium]
                                     : [NSFont systemFontOfSize:11 weight:NSFontWeightRegular];
    NSDictionary *attrs = @{NSFontAttributeName: font,
                             NSForegroundColorAttributeName: textColor,
                             NSParagraphStyleAttributeName: ps};
    CGFloat textX = 8 + kIconSize + 4;
    CGFloat rightPad = kCloseSize + 8 + (_isPinned ? kPinSize + 2 : 0);
    CGFloat textW = w - textX - rightPad;
    CGFloat textY = _isSelected ? 3.0 : 1.5;  // bottom space (active 3.0, inactive 1.5)
    [_title drawInRect:NSMakeRect(textX, textY, textW, font.pointSize + 4)
        withAttributes:attrs];

    // ── Close button (rightmost, hidden if pref is off) ─────────────────────
    BOOL showClose = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefTabCloseButton];
    CGFloat cx = w - kCloseSize - 6;
    CGFloat cy = (h - kCloseSize) / 2.0;
    if (showClose && (_isSelected || _hovered)) {
        NSImage *closeImg = nil;
        if (_closeHovered)    closeImg = tabIcon(@"closeTabButton_hoverIn");
        else if (_isSelected) closeImg = tabIcon(@"closeTabButton");
        else                  closeImg = tabIcon(@"closeTabButton_hoverOnTab");
        if (closeImg) { closeImg.size = NSMakeSize(32, 32);
            [closeImg drawInRect:NSMakeRect(cx, cy, kCloseSize, kCloseSize)
                       fromRect:NSZeroRect
                      operation:NSCompositingOperationSourceOver fraction:1.0];
        } else {
            NSDictionary *xa = @{NSFontAttributeName: [NSFont systemFontOfSize:11],
                                  NSForegroundColorAttributeName: textColor};
            [@"×" drawAtPoint:NSMakePoint(cx + 1, cy - 1) withAttributes:xa];
        }
    }

    // ── Pin icon (to the left of close button, only when pinned) ─────────────
    if (_isPinned) {
        NSString *pinPath = [[NSBundle mainBundle] pathForResource:@"pinTabButton_pinned" ofType:@"png"
                                                       inDirectory:@"icons/standard/tabbar"];
        NSImage *pinImg = pinPath ? [[NSImage alloc] initWithContentsOfFile:pinPath] : nil;
        if (pinImg) {
            CGFloat px = cx - kPinSize - 2;
            CGFloat py = (h - kPinSize) / 2.0;
            [pinImg drawInRect:NSMakeRect(px, py, kPinSize, kPinSize)
                     fromRect:NSZeroRect
                    operation:NSCompositingOperationSourceOver fraction:1.0];
        }
    }
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:(NSTrackingMouseEnteredAndExited |
                      NSTrackingMouseMoved            |
                      NSTrackingActiveInKeyWindow)
               owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)mouseEntered:(NSEvent *)e { _hovered = YES;  [self setNeedsDisplay:YES]; }
- (void)mouseExited:(NSEvent *)e  { _hovered = NO; _closeHovered = NO; [self setNeedsDisplay:YES]; }
- (void)mouseMoved:(NSEvent *)e {
    NSPoint p  = [self convertPoint:e.locationInWindow fromView:nil];
    CGFloat cx = self.bounds.size.width - kCloseSize - 6;
    BOOL oc    = p.x >= cx && p.x <= cx + kCloseSize;
    if (oc != _closeHovered) { _closeHovered = oc; [self setNeedsDisplay:YES]; }
}

- (void)mouseDown:(NSEvent *)event {
    NSPoint p  = [self convertPoint:event.locationInWindow fromView:nil];
    CGFloat cx = self.bounds.size.width - kCloseSize - 6;
    BOOL closeVisible = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefTabCloseButton];
    BOOL overClose = closeVisible && (_isSelected || _hovered)
                     && p.x >= cx && p.x <= cx + kCloseSize;
    // Double-click anywhere on tab to close (if enabled)
    if (!overClose && event.clickCount == 2 &&
        [[NSUserDefaults standardUserDefaults] boolForKey:kPrefDoubleClickTabClose]) {
        overClose = YES;
    }
    SEL action = overClose ? _closeAction : _selectAction;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    [_target performSelector:action withObject:self];
#pragma clang diagnostic pop
}

- (NSMenu *)menuForEvent:(NSEvent *)event {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    [_target performSelector:_selectAction withObject:self];
#pragma clang diagnostic pop
    return [(NppTabBar *)_target buildTabContextMenu];
}

@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - _NppTabBarContainer
//
// Document view that backs the tab bar's scroll view and holds the
// _NppTabItem subviews. Its sole responsibility beyond being a plain NSView
// is to detect a double-click on the empty space to the right of the last
// tab (or below the last row in wrap mode) and ask its owning NppTabBar
// to fire `tabBarDidRequestNewTab:` on the delegate.
//
// AppKit's hit-test guarantees that mouseDown only reaches us when the click
// did NOT land on any _NppTabItem subview — so we don't need any
// point-in-rect math to know "the click was on empty space."
//
// Same-view double-click guard:
//   NSEvent.clickCount is window-scoped, not view-scoped. A user click
//   sequence like `tab → empty area within 400 ms` would deliver a
//   clickCount=2 event to us even though click #1 landed on a different
//   view (the tab). Without this guard, that sequence would spuriously
//   open a new tab. We track the timestamp of every mouseDown WE receive
//   and only fire on clickCount=2 if the previous click was also ours
//   within [NSEvent doubleClickInterval]. Click sequences that don't
//   originate inside this view never trigger the gesture.

// Private NppTabBar API used by _NppTabBarContainer below. Declared in a
// class extension so the container's mouseDown can call it under ARC
// without falling back to performSelector.
@interface NppTabBar (_NppTabBarPrivate)
- (void)_emptyAreaDoubleClicked;
@end

@interface _NppTabBarContainer : NSView
@property (nonatomic, weak, nullable) NppTabBar *tabBar;
@end

@implementation _NppTabBarContainer {
    NSTimeInterval _lastClickHere;  // timestamp of last mouseDown delivered to us
}

- (void)mouseDown:(NSEvent *)event {
    NSTimeInterval now = event.timestamp;

    if (event.clickCount == 2 &&
        _lastClickHere > 0 &&
        (now - _lastClickHere) <= [NSEvent doubleClickInterval])
    {
        // Both clicks of the pair landed on us → genuine empty-area
        // double-click. Reset the timestamp so a subsequent quick click
        // doesn't pair with this completed gesture.
        _lastClickHere = 0;
        if (self.tabBar) [self.tabBar _emptyAreaDoubleClicked];
        return;
    }

    // Single-click on empty area, or first click of a future pair.
    // Stash the timestamp so the same-view guard above can recognise the
    // pair if a second click follows in time.
    _lastClickHere = now;
}

@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - NppTabBar

@implementation NppTabBar {
    NSScrollView                  *_scrollView;
    _NppTabBarContainer           *_containerView;
    NSMutableArray<_NppTabItem *> *_items;
    NSInteger                      _selectedIndex;
    BOOL                           _wrapMode;
    CGFloat                        _preferredHeight;
    _NppScrollArrowButton         *_scrollLeftBtn;
    _NppScrollArrowButton         *_scrollRightBtn;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _items         = [NSMutableArray array];
        _selectedIndex = -1;
        _preferredHeight = kTabBarBaseHeight;
        [self _buildUI];
        [[NSNotificationCenter defaultCenter]
            addObserver:self selector:@selector(_darkModeChanged:)
                   name:NPPDarkModeChangedNotification object:nil];
    }
    return self;
}

- (void)_darkModeChanged:(NSNotification *)n {
    // Redraw all tabs and the bar itself with updated theme colors
    for (_NppTabItem *item in _items)
        [item setNeedsDisplay:YES];
    [_scrollLeftBtn setNeedsDisplay:YES];
    [_scrollRightBtn setNeedsDisplay:YES];
    [self setNeedsDisplay:YES];
}

- (void)_buildUI {
    _containerView = [[_NppTabBarContainer alloc] initWithFrame:NSZeroRect];
    _containerView.tabBar = self;

    _scrollView                       = [[NSScrollView alloc] initWithFrame:self.bounds];
    _scrollView.autoresizingMask      = NSViewNotSizable;   // managed in relayout
    _scrollView.hasHorizontalScroller = NO;
    _scrollView.hasVerticalScroller   = NO;
    _scrollView.drawsBackground       = NO;
    _scrollView.documentView          = _containerView;
    [self addSubview:_scrollView];

    _scrollLeftBtn  = [[_NppScrollArrowButton alloc] initPointingRight:NO
                                                                target:self
                                                                action:@selector(_scrollLeft:)];
    _scrollRightBtn = [[_NppScrollArrowButton alloc] initPointingRight:YES
                                                                target:self
                                                                action:@selector(_scrollRight:)];
    [self addSubview:_scrollLeftBtn];
    [self addSubview:_scrollRightBtn];
}

// Legacy alias — kept so any external caller still compiles.
- (void)buildScrollView { /* init already called _buildUI */ }

- (void)drawRect:(NSRect)dirtyRect {
    [tabBarBgColor() setFill];
    NSRectFill(self.bounds);
    [[NSColor separatorColor] setFill];
    NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 1));
}

// Called by Auto Layout whenever the view is sized — use this to guarantee
// relayout runs with correct bounds (fixes arrow visibility without window resize).
- (void)layout {
    [super layout];
    [self relayout];
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    [self relayout];
}

- (NSSize)intrinsicContentSize {
    return NSMakeSize(NSViewNoIntrinsicMetric, _preferredHeight);
}

#pragma mark - Public API

- (void)addTabWithTitle:(NSString *)title modified:(BOOL)modified {
    _NppTabItem *item  = [[_NppTabItem alloc] initWithFrame:NSZeroRect];
    item.title         = title;
    item.isModified    = modified;
    item.isSelected    = NO;
    item.tabIndex      = _items.count;
    item.target        = self;
    item.selectAction  = @selector(tabItemSelected:);
    item.closeAction   = @selector(tabItemClosed:);
    [_items addObject:item];
    [_containerView addSubview:item];
    [self relayout];
    [self setNeedsLayout:YES];   // schedule Auto Layout pass → layout → relayout
}

- (void)removeTabAtIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_items.count) return;
    [_items[index] removeFromSuperview];
    [_items removeObjectAtIndex:index];
    for (NSInteger i = index; i < (NSInteger)_items.count; i++)
        _items[i].tabIndex = i;
    if (_selectedIndex >= (NSInteger)_items.count)
        _selectedIndex = (NSInteger)_items.count - 1;
    if (_selectedIndex >= 0)
        _items[_selectedIndex].isSelected = YES;
    [self relayout];
    [self setNeedsLayout:YES];
}

- (void)setTitle:(NSString *)title modified:(BOOL)modified atIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_items.count) return;
    _items[index].title      = title;
    _items[index].isModified = modified;
    [_items[index] setNeedsDisplay:YES];
}

- (void)selectTabAtIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_items.count) return;
    if (_selectedIndex >= 0 && _selectedIndex < (NSInteger)_items.count) {
        _items[_selectedIndex].isSelected = NO;
        [_items[_selectedIndex] setNeedsDisplay:YES];
    }
    _selectedIndex                = index;
    _items[index].isSelected      = YES;
    [_items[index] setNeedsDisplay:YES];
    [self relayout];
    [self scrollTabToVisible:index];
}

- (NSInteger)tabCount { return (NSInteger)_items.count; }

- (void)pinTabAtIndex:(NSInteger)index toggle:(BOOL)toggle {
    if (index < 0 || index >= (NSInteger)_items.count) return;
    _items[index].isPinned = toggle;
    [_items[index] setNeedsDisplay:YES];
    [self relayout];
}

- (BOOL)wrapMode { return _wrapMode; }
- (void)setWrapMode:(BOOL)wrap {
    if (_wrapMode == wrap) return;
    _wrapMode = wrap;
    [self relayout];
    [self invalidateIntrinsicContentSize];
    [self setNeedsDisplay:YES];
}

- (BOOL)isTabPinnedAtIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_items.count) return NO;
    return _items[index].isPinned;
}

- (void)swapTabAtIndex:(NSInteger)a withIndex:(NSInteger)b {
    NSInteger count = (NSInteger)_items.count;
    if (a < 0 || a >= count || b < 0 || b >= count || a == b) return;
    [_items exchangeObjectAtIndex:(NSUInteger)a withObjectAtIndex:(NSUInteger)b];
    // Re-assign tabIndex to match new positions
    _items[a].tabIndex = a;
    _items[b].tabIndex = b;
    [self relayout];
}

- (void)setTabColorAtIndex:(NSInteger)index colorId:(NSInteger)colorId {
    if (index < 0 || index >= (NSInteger)_items.count) return;
    _items[index].colorId = colorId;
    [_items[index] setNeedsDisplay:YES];
}

- (NSInteger)tabColorAtIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_items.count) return -1;
    return _items[index].colorId;
}

#pragma mark - Tab item callbacks

- (void)tabItemSelected:(_NppTabItem *)item {
    if (item.tabIndex != _selectedIndex)
        [self selectTabAtIndex:item.tabIndex];
    // Always fire delegate — even for already-selected tabs — so that
    // _activeTabManager updates when the user clicks/right-clicks in any pane.
    [_delegate tabBar:self didSelectTabAtIndex:item.tabIndex];
}

- (void)tabItemClosed:(_NppTabItem *)item {
    [_delegate tabBar:self didCloseTabAtIndex:item.tabIndex];
}

#pragma mark - Layout

- (CGFloat)_preferredHeightForWidth:(CGFloat)barW {
    if (!_wrapMode || _items.count == 0) return kTabBarBaseHeight;

    CGFloat inactiveH = kTabBarBaseHeight - kTabTopGap - 1;
    CGFloat activeH   = inactiveH + kActiveBoost;
    CGFloat rowStep   = activeH + 1;
    CGFloat x = 0;
    NSInteger rows = 1;

    for (_NppTabItem *item in _items) {
        CGFloat w = item.preferredWidth;
        if (x + w > barW && x > 0) {
            x = 0;
            rows++;
        }
        x += w;
    }

    return 1 + ((CGFloat)rows - 1) * rowStep + activeH + (kTabTopGap - kActiveBoost);
}

- (void)_setPreferredHeight:(CGFloat)height {
    height = MAX(kTabBarBaseHeight, ceil(height));
    if (fabs(_preferredHeight - height) < 0.5) return;

    _preferredHeight = height;
    [self invalidateIntrinsicContentSize];
    [self.superview setNeedsLayout:YES];
}

- (void)relayout {
    CGFloat barW = self.bounds.size.width;
    CGFloat barH = self.bounds.size.height;
    if (barW < 1 || barH < 1) return;  // not yet sized — skip

    CGFloat inactiveH = kTabBarBaseHeight - kTabTopGap - 1; // visible inactive tab height
    CGFloat activeH   = inactiveH + kActiveBoost;          // active tab is slightly taller

    if (_wrapMode) {
        CGFloat neededH = [self _preferredHeightForWidth:barW];
        [self _setPreferredHeight:neededH];

        _scrollLeftBtn.hidden  = YES;
        _scrollRightBtn.hidden = YES;
        _scrollView.frame = NSMakeRect(0, 0, barW, barH);
        [_scrollView.contentView scrollToPoint:NSZeroPoint];

        CGFloat x = 0;
        CGFloat rowStep = activeH + 1;
        NSInteger row = 0;
        for (_NppTabItem *item in _items) {
            CGFloat w = item.preferredWidth;
            if (x + w > barW && x > 0) { x = 0; row++; }
            BOOL sel = (item.tabIndex == _selectedIndex);
            CGFloat y = neededH - (kTabTopGap - kActiveBoost) - activeH - ((CGFloat)row * rowStep);
            item.frame = NSMakeRect(x, y, w, sel ? activeH : inactiveH);
            x += w;
        }
        _containerView.frame = NSMakeRect(0, 0, barW, neededH);
        [self setNeedsDisplay:YES];
        return;
    }

    [self _setPreferredHeight:kTabBarBaseHeight];

    // ── Non-wrap: calculate total tab width, decide if arrows needed ──────────
    CGFloat totalTabsW = 0;
    for (_NppTabItem *item in _items) totalTabsW += item.preferredWidth;

    BOOL    needsArrows = (totalTabsW > barW);
    CGFloat arrowsW     = needsArrows ? (2.0 * kArrowBtnW) : 0.0;
    CGFloat scrollW     = barW - arrowsW;

    _scrollView.frame = NSMakeRect(0, 0, scrollW, barH);

    _scrollLeftBtn.hidden  = !needsArrows;
    _scrollRightBtn.hidden = !needsArrows;
    if (needsArrows) {
        _scrollLeftBtn.frame  = NSMakeRect(scrollW,              0, kArrowBtnW, barH);
        _scrollRightBtn.frame = NSMakeRect(scrollW + kArrowBtnW, 0, kArrowBtnW, barH);
    }

    // Position tabs: inactive at y=1; active at y=1 but taller (raised look)
    CGFloat x = 0;
    for (_NppTabItem *item in _items) {
        CGFloat w  = item.preferredWidth;
        BOOL    sel = (item.tabIndex == _selectedIndex);
        item.frame  = NSMakeRect(x, 1, w, sel ? activeH : inactiveH);
        x += w;
    }
    _containerView.frame = NSMakeRect(0, 0, MAX(x, scrollW), barH);
    [self setNeedsDisplay:YES];
}

// Minimal-scroll: only move the viewport if the tab isn't already fully visible.
// New tabs added at right edge scroll into view from the right — never push
// existing tabs off the left.
- (void)scrollTabToVisible:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_items.count) return;
    if (_wrapMode) {
        [_scrollView.contentView scrollToPoint:NSZeroPoint];
        [_scrollView reflectScrolledClipView:_scrollView.contentView];
        return;
    }
    NSRect     tab = _items[index].frame;
    NSClipView *cv = _scrollView.contentView;
    CGFloat     cx = cv.bounds.origin.x;
    CGFloat     sw = _scrollView.bounds.size.width;
    CGFloat     nx = cx;

    if (NSMinX(tab) < cx)           // tab is off the left edge
        nx = NSMinX(tab);
    else if (NSMaxX(tab) > cx + sw) // tab is off the right edge
        nx = NSMaxX(tab) - sw;

    if (nx != cx) {
        [cv scrollToPoint:NSMakePoint(MAX(0, nx), 0)];
        [_scrollView reflectScrolledClipView:cv];
    }
}

#pragma mark - Empty-area gesture

// Called from _NppTabBarContainer when the user double-clicks empty space
// to the right of the last tab (or below the last row in wrap mode). The
// container has already validated that both clicks of the pair landed on
// itself — no further geometry checks needed here.
- (void)_emptyAreaDoubleClicked {
    if ([self.delegate respondsToSelector:@selector(tabBarDidRequestNewTab:)])
        [self.delegate tabBarDidRequestNewTab:self];
}

#pragma mark - Scroll actions

- (void)_scrollLeft:(id)sender {
    NSClipView *cv  = _scrollView.contentView;
    CGFloat     cur = cv.bounds.origin.x;
    [cv scrollToPoint:NSMakePoint(MAX(0, cur - kTabMinWidth), 0)];
    [_scrollView reflectScrolledClipView:cv];
}

- (void)_scrollRight:(id)sender {
    NSClipView *cv   = _scrollView.contentView;
    CGFloat     cur  = cv.bounds.origin.x;
    CGFloat     maxX = MAX(0, _containerView.frame.size.width - _scrollView.bounds.size.width);
    [cv scrollToPoint:NSMakePoint(MIN(maxX, cur + kTabMinWidth), 0)];
    [_scrollView reflectScrolledClipView:cv];
}

#pragma mark - Context menu

/// Walk a menu recursively to find an item by title (case-insensitive, strips shortcuts).
static NSMenuItem *_findMenuItemByTitle(NSMenu *menu, NSString *title) {
    NSString *target = title.lowercaseString;
    for (NSMenuItem *mi in menu.itemArray) {
        if (mi.isSeparatorItem) continue;
        // Strip keyboard shortcut suffix (everything after \t) and & accelerator markers
        NSString *clean = mi.title;
        NSRange tabR = [clean rangeOfString:@"\t"];
        if (tabR.location != NSNotFound) clean = [clean substringToIndex:tabR.location];
        clean = [clean stringByReplacingOccurrencesOfString:@"&" withString:@""];
        clean = [clean stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];

        if ([clean.lowercaseString isEqualToString:target]) return mi;

        // Recurse into submenus
        if (mi.submenu) {
            NSMenuItem *found = _findMenuItemByTitle(mi.submenu, title);
            if (found) return found;
        }
    }
    return nil;
}

/// Load tab context menu from XML. Returns nil if file not found or parse fails.
static NSMenu *_buildTabContextMenuFromXML(NSString *xmlPath) {
    NSData *data = [NSData dataWithContentsOfFile:xmlPath];
    if (!data) return nil;

    NSXMLDocument *doc = [[NSXMLDocument alloc] initWithData:data options:0 error:nil];
    if (!doc) return nil;

    NSArray *items = [doc nodesForXPath:@"//TabContextMenu/Item" error:nil];
    if (!items.count) return nil;

    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenu *contextMenu = [[NSMenu alloc] initWithTitle:@""];
    NSMutableDictionary<NSString *, NSMenu *> *folders = [NSMutableDictionary dictionary];
    // Track folder insertion order for consistent submenu placement
    NSMutableArray<NSString *> *folderOrder = [NSMutableArray array];

    for (NSXMLElement *el in items) {
        NSString *folderName = [[el attributeForName:@"FolderName"] stringValue];
        NSString *menuEntry  = [[el attributeForName:@"MenuEntryName"] stringValue];
        NSString *menuItem   = [[el attributeForName:@"MenuItemName"] stringValue];
        NSString *displayAs  = [[el attributeForName:@"ItemNameAs"] stringValue];
        NSString *builtIn    = [[el attributeForName:@"BuiltIn"] stringValue];
        NSInteger itemId     = [[[el attributeForName:@"id"] stringValue] integerValue];

        // Separator
        if ([[el attributeForName:@"id"] stringValue] && itemId == 0) {
            NSMenu *target = folderName.length ? folders[folderName] : contextMenu;
            if (target) [target addItem:[NSMenuItem separatorItem]];
            continue;
        }

        // Built-in special commands (not in main menu)
        if (builtIn.length) {
            if ([builtIn isEqualToString:@"PinTab"]) {
                NSMenuItem *pinItem = [[NSMenuItem alloc] initWithTitle:@"Pin Tab"
                                                                action:@selector(pinCurrentTab:)
                                                         keyEquivalent:@""];
                [contextMenu addItem:pinItem];
            }
            continue;
        }

        if (!menuEntry.length || !menuItem.length) continue;

        // Find the top-level menu matching MenuEntryName
        NSMenu *entryMenu = nil;
        for (NSMenuItem *top in mainMenu.itemArray) {
            NSString *title = top.submenu.title ?: top.title;
            if ([title.lowercaseString isEqualToString:menuEntry.lowercaseString]) {
                entryMenu = top.submenu;
                break;
            }
        }
        if (!entryMenu) continue;

        // Find the specific item within that menu (recursive search)
        NSMenuItem *found = _findMenuItemByTitle(entryMenu, menuItem);
        if (!found || !found.action) continue;

        // Build context menu item with the resolved action
        NSString *title = displayAs.length ? displayAs : found.title;
        NSMenuItem *ctxItem = [[NSMenuItem alloc] initWithTitle:title
                                                         action:found.action
                                                  keyEquivalent:@""];
        ctxItem.target = found.target;

        // Copy color swatch image from main menu item (for Apply Color items)
        if (found.image) ctxItem.image = found.image;

        // Add to folder submenu or top level
        if (folderName.length) {
            if (!folders[folderName]) {
                folders[folderName] = [[NSMenu alloc] initWithTitle:folderName];
                [folderOrder addObject:folderName];
                NSMenuItem *parent = [[NSMenuItem alloc] initWithTitle:folderName
                                                                action:nil keyEquivalent:@""];
                parent.submenu = folders[folderName];
                parent.tag = 99000 + (NSInteger)folderOrder.count; // unique tag for ordering
                [contextMenu addItem:parent];
            }
            [folders[folderName] addItem:ctxItem];
        } else {
            [contextMenu addItem:ctxItem];
        }
    }

    // Clean up: remove trailing/leading/duplicate separators
    while (contextMenu.numberOfItems > 0 && [contextMenu itemAtIndex:0].isSeparatorItem)
        [contextMenu removeItemAtIndex:0];
    while (contextMenu.numberOfItems > 0 &&
           [contextMenu itemAtIndex:contextMenu.numberOfItems - 1].isSeparatorItem)
        [contextMenu removeItemAtIndex:contextMenu.numberOfItems - 1];

    return contextMenu.numberOfItems > 0 ? contextMenu : nil;
}

- (NSMenu *)buildTabContextMenu {
    // Try user-customized tabContextMenu.xml first
    NSString *configDir = [NSHomeDirectory() stringByAppendingPathComponent:@".notepad++"];
    NSString *customPath = [configDir stringByAppendingPathComponent:@"tabContextMenu.xml"];
    NSMenu *menu = _buildTabContextMenuFromXML(customPath);
    if (menu) return menu;

    // Fall back to bundled default
    NSString *bundledPath = [[NSBundle mainBundle] pathForResource:@"tabContextMenu" ofType:@"xml"];
    menu = _buildTabContextMenuFromXML(bundledPath);
    if (menu) return menu;

    // Ultimate fallback: minimal hardcoded menu
    menu = [[NSMenu alloc] initWithTitle:@""];
    [menu addItemWithTitle:@"Close" action:@selector(closeCurrentTab:) keyEquivalent:@""];
    [menu addItemWithTitle:@"Save" action:@selector(saveDocument:) keyEquivalent:@""];
    return menu;
}

@end
