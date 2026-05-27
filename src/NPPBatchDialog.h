// Configuration dialog for "Run Macro on Files." Single-window flow with two
// visual states: configure (pre-Run) and progress (post-Run, in-flight).
// Closes itself when the user dismisses; spawns an NSAlert summary at the end.
//
// See docs/RFC_BATCH_MACRO_ON_FILES.md.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class MainWindowController;

@interface NPPBatchDialog : NSWindowController

/// Show the dialog in folder-enumeration mode. If `preselectedFolder` is
/// non-nil, the folder picker is pre-filled with it (used by the
/// Folder-as-Workspace context menu). nil = empty, user picks via Browse.
+ (void)presentForWindow:(MainWindowController *)mwc
        preselectedFolder:(nullable NSString *)preselectedFolder;

/// Show the dialog in fixed-file-list mode. Used by the Project Panel,
/// whose workspaces are XML-based *virtual* trees — files can live in
/// arbitrary directory locations, so there's no single folder to enumerate.
/// The caller passes the pre-collected absolute paths and a human-readable
/// description ("Project: MyProj") that replaces the folder field.
/// Extensions / max-size / per-file / error-policy controls still apply.
/// Recurse and hidden-files toggles are hidden (irrelevant to a flat list).
+ (void)presentForWindow:(MainWindowController *)mwc
                    files:(NSArray<NSString *> *)files
                   source:(NSString *)sourceDescription;

@end

NS_ASSUME_NONNULL_END
