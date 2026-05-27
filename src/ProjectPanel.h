#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class ProjectPanel;

@protocol ProjectPanelDelegate <NSObject>
- (void)projectPanel:(ProjectPanel *)panel openFileAtPath:(NSString *)path;
- (void)projectPanel:(ProjectPanel *)panel findInFilesAtPath:(NSString *)path;
@optional
/// Open the "Run Macro on Files" dialog with the given (already-flattened) file
/// list as the input set. `sourceDescription` is a user-facing label shown in
/// the dialog where the folder field normally lives, e.g. "Project: MyApp".
- (void)projectPanel:(ProjectPanel *)panel runMacroOnFiles:(NSArray<NSString *> *)files
                                            sourceDescription:(NSString *)description;
@end

/// Project Panel — virtual workspace tree with projects, folders, and files.
/// Contains 3 independent workspaces switchable via bottom segment control.
@interface ProjectPanel : NSView <NSOutlineViewDataSource, NSOutlineViewDelegate>

@property (nonatomic, weak, nullable) id<ProjectPanelDelegate> delegate;

/// Switch to workspace tab (0, 1, or 2) and show it.
- (void)activateTab:(NSInteger)tabIndex;

/// Current active tab index (0-2).
@property (nonatomic, readonly) NSInteger activeTab;

/// Returns all file paths from the workspace at the given tab index (0-2).
/// Returns empty array if the workspace has no loaded XML or no files.
- (NSArray<NSString *> *)allFilePathsFromWorkspace:(NSInteger)tabIndex;

/// Returns YES if the workspace at the given tab has a loaded XML with files.
- (BOOL)workspaceHasContent:(NSInteger)tabIndex;

/// Called by MainWindowController before this panel is removed from the
/// SidePanelHost (either via the PanelFrame X button or via a tab toggle-off).
/// Flushes dirty workspaces and persists the workspace path preferences.
- (void)panelWillClose;

@end

NS_ASSUME_NONNULL_END
