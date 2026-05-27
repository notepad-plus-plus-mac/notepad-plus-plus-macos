// Run a saved macro across a list of files. Headless engine — no UI
// dependencies. The configuration dialog (NPPBatchDialog) and progress
// sheet (NPPBatchProgressSheet) are separate; this class is testable
// from a menu wiring alone.
//
// Synchronous, main-thread only. Scintilla is not thread-safe, so each
// per-file iteration (open → run macro → save → close) runs on the same
// thread that started the run. The run loop is pumped between files so
// the progress UI stays responsive and Esc can cancel.
//
// See docs/RFC_BATCH_MACRO_ON_FILES.md for the design rationale.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class MainWindowController;

typedef NS_ENUM(NSInteger, NPPBatchErrorPolicy) {
    NPPBatchErrorPolicySkip = 0,    ///< Skip the file and continue with the next.
    NPPBatchErrorPolicyStop = 1,    ///< Halt the loop on the first error.
};

/// What happened to one file in the batch.
typedef NS_ENUM(NSInteger, NPPBatchOutcome) {
    NPPBatchOutcomeOk = 0,
    NPPBatchOutcomeFailedToOpen,    ///< openFileAtPath: returned nil.
    NPPBatchOutcomeMacroFailed,     ///< runMacroActions: threw or refused.
    NPPBatchOutcomeSaveFailed,      ///< Disk write failed (read-only, full, …).
    NPPBatchOutcomeSkippedTooBig,   ///< Exceeded options.maxFileSizeBytes.
    NPPBatchOutcomeSkippedPreExisting, ///< Tab was already open; closePreExistingTabs=NO kept it.
    NPPBatchOutcomeCancelled,       ///< User pressed Esc / Cancel mid-file.
};

/// Per-file outcome record.
@interface NPPBatchRunResult : NSObject
@property (nonatomic, copy)   NSString *filePath;
@property (nonatomic)         NPPBatchOutcome outcome;
@property (nonatomic, copy, nullable) NSString *errorMessage;
@end

/// Input configuration for one batch run.
@interface NPPBatchOptions : NSObject
/// The files to process, in order. The runner does NOT enumerate or filter —
/// the caller (typically NPPBatchDialog) is responsible for the file list.
@property (nonatomic, strong) NSArray<NSString *> *filePaths;

/// The macro action stream, in the same XML-format-dict shape that
/// MainWindowController.loadMacrosFromShortcutsXML returns (key "actions").
@property (nonatomic, strong) NSArray<NSDictionary *> *macroActions;

/// Save the buffer back to disk after the macro finishes, if the buffer
/// got modified. Files whose content didn't change are NOT rewritten.
@property (nonatomic) BOOL saveAfter;

/// Close the tab once the per-file pipeline completes. See
/// closePreExistingTabs for the "tab was open before the batch" exception.
@property (nonatomic) BOOL closeAfter;

/// If a target file was already open in a tab when the batch started AND
/// closeAfter is YES, this flag decides whether to close that tab too.
/// Default NO: we leave the user's previously-open tabs alone and record
/// NPPBatchOutcomeSkippedPreExisting so the user can review afterwards.
@property (nonatomic) BOOL closePreExistingTabs;

/// Reject files larger than this size (in bytes). 0 = unlimited.
@property (nonatomic) NSInteger maxFileSizeBytes;

@property (nonatomic) NPPBatchErrorPolicy errorPolicy;
@end

/// Synchronous batch runner. Lifetime: created, runWithProgress:completion:
/// invoked once, discarded. Not reusable across runs.
@interface NPPBatchRunner : NSObject

+ (instancetype)runnerForWindow:(MainWindowController *)mwc
                        options:(NPPBatchOptions *)opts;

/// Kick off the batch. Returns after the last file (or after cancellation).
/// `progress` is invoked on the main thread before each file is processed,
/// then once again after it with the cumulative counts updated.
/// `completion` is invoked once at the end with the full results array and
/// whether the user cancelled mid-run.
- (void)runWithProgress:(void(^)(NSInteger done, NSInteger total,
                                  NSString *currentPath,
                                  NSInteger ok, NSInteger skipped, NSInteger failed))progress
             completion:(void(^)(NSArray<NPPBatchRunResult *> *results,
                                 BOOL cancelled))completion;

/// Request cancellation. The current file finishes, then the loop exits.
/// Idempotent; safe from any thread (sets a __block flag).
- (void)cancel;

@end

NS_ASSUME_NONNULL_END
