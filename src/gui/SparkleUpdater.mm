/*****************************************************************************
   Project: CEDAR Logic Simulator
   SparkleUpdater: macOS auto-update support via Sparkle framework
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

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

    updaterController = [[SPUStandardUpdaterController alloc]
        initWithStartingUpdater:YES
        updaterDelegate:nil
        userDriverDelegate:nil];
}

void SparkleUpdater_CheckForUpdates() {
    if (updaterController != nil) {
        [updaterController checkForUpdates:nil];
        return;
    }
    // Asked for explicitly (Help > Download Latest Version) in a build with no
    // updater. Say so, rather than having the menu item do nothing at all.
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Updates are not available in this build.";
    alert.informativeText = @"This copy of CedarLogic was built locally, so it "
                            @"cannot verify or install updates. Official "
                            @"releases update themselves.";
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

#endif // __APPLE__
