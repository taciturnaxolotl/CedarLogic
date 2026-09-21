/*****************************************************************************
   Project: CEDAR Logic Simulator
   SparkleUpdater: macOS auto-update support via Sparkle framework
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

#include <sys/mount.h>   // statfs, for the read-only test below

#include <string>

static SPUStandardUpdaterController *updaterController = nil;

// Sparkle authenticates every update against the EdDSA public key in Info.plist
// (SUPublicEDKey, filled from the SPARKLE_ED_PUBLIC_KEY cache variable). Only
// the release workflow supplies it, from a secret, so a local build has an empty
// one -- and Sparkle refuses to start without a key, putting up "The update
// checker failed to start correctly" at every launch. A dev build has nothing to
// update itself to anyway, so leave the updater alone rather than starting it to
// fail.
static bool updaterConfigured() {
    NSString *key = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"SUPublicEDKey"];
    return key.length > 0;
}

// Whether this copy could ever install an update over itself: a disk image it
// was opened from without being dragged out, or Gatekeeper's read-only copy of
// a quarantined app. Sparkle tests the same thing at the top of every check and
// aborts with "opened from a read-only or a temporary location", which a
// background check puts on screen unasked. Testing it first keeps that to the
// times someone asked for an update.
//
// Sparkle ignores what statfs returns; a failed stat is read as writable here
// rather than costing someone their updates.
static bool runningFromReadOnlyLocation() {
    struct statfs info;
    NSString *path = [[NSBundle mainBundle] bundlePath];
    if (statfs(path.fileSystemRepresentation, &info) != 0) return false;
    return (info.f_flags & MNT_RDONLY) != 0;
}

// Read the appcast without going through Sparkle, for the crash dialog that
// wants to name a fix version before it offers to file a report (see
// UpdateInfo.h). Synchronous, so it must be called from a worker thread: it
// parks a semaphore until the transfer finishes or times out.
std::string cl_update_fetch_appcast_mac(const std::string &url) {
    @autoreleasepool {
        NSURL *u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (u == nil) return std::string();

        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:u];
        req.timeoutInterval = 10.0;
        req.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;

        __block std::string body;
        __block BOOL ok = NO;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSURLSessionTask *task = [[NSURLSession sharedSession]
            dataTaskWithRequest:req
              completionHandler:^(NSData *data, NSURLResponse *resp, NSError *err) {
                NSHTTPURLResponse *http = [resp isKindOfClass:[NSHTTPURLResponse class]]
                                              ? (NSHTTPURLResponse *)resp : nil;
                if (!err && http && http.statusCode == 200 && data.length > 0 &&
                    data.length < (1u << 20)) {
                    body.assign((const char *)data.bytes, data.length);
                    ok = YES;
                }
                dispatch_semaphore_signal(done);
              }];
        [task resume];
        dispatch_semaphore_wait(done,
            dispatch_time(DISPATCH_TIME_NOW, (int64_t)(15 * NSEC_PER_SEC)));
        return ok ? body : std::string();
    }
}

void SparkleUpdater_Initialize() {
    if (updaterController != nil || !updaterConfigured()) return;
    if (runningFromReadOnlyLocation()) return;

    updaterController = [[SPUStandardUpdaterController alloc]
        initWithStartingUpdater:YES
        updaterDelegate:nil
        userDriverDelegate:nil];
}

// Updates are the smaller half of the problem. The app's code pages are backed
// by that read-only mount, so ejecting the disk image mid-session leaves every
// later page fault with nowhere to go and the process hangs for good -- a
// beachball, and whatever was unsaved. Worth a word before that happens.
//
// Not remembered across launches: the hazard lasts as long as the app sits
// there, so a "do not show again" would silence something still true. Moving
// the app stops it.
void SparkleUpdater_WarnIfReadOnlyLocation() {
    if (!runningFromReadOnlyLocation()) return;

    NSAlert *alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = @"Move CedarLogic to your Applications folder.";
    alert.informativeText = @"This copy is running from a disk image or another "
                            @"read-only location. If that location disappears "
                            @"while CedarLogic is open, the app will stop "
                            @"responding and unsaved work may be lost. Quit "
                            @"CedarLogic, drag it into Applications, and open it "
                            @"from there.";
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

void SparkleUpdater_CheckForUpdates() {
    if (updaterController != nil) {
        [updaterController checkForUpdates:nil];
        return;
    }
    // Asked for explicitly (Help > Download Latest Version) in a build with no
    // updater. Say so, rather than having the menu item do nothing at all.
    NSAlert *alert = [[NSAlert alloc] init];
    if (updaterConfigured() && runningFromReadOnlyLocation()) {
        // A release build that is simply in the wrong place. The remedy is the
        // same whether it is a disk image or Gatekeeper's own read-only copy,
        // so both get the one message.
        alert.messageText = @"Move CedarLogic to your Applications folder to "
                            @"update it.";
        alert.informativeText = @"This copy is running from a disk image or "
                                @"another read-only location, so it cannot "
                                @"update itself. Quit CedarLogic, drag it into "
                                @"Applications, and open it from there.";
    } else {
        alert.messageText = @"Updates are not available in this build.";
        alert.informativeText = @"This copy of CedarLogic was built locally, so "
                                @"it cannot verify or install updates.";
    }
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

#endif // __APPLE__
