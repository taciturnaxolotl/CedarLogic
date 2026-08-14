/*****************************************************************************
   Project: CEDAR Logic Simulator
   SparkleUpdater: macOS auto-update support via Sparkle framework
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

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
