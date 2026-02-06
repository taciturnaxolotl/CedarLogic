/*****************************************************************************
   Project: CEDAR Logic Simulator
   SparkleUpdater: macOS auto-update support via Sparkle framework
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

static SPUStandardUpdaterController *updaterController = nil;

void SparkleUpdater_Initialize() {
    if (updaterController == nil) {
        updaterController = [[SPUStandardUpdaterController alloc]
            initWithStartingUpdater:YES
            updaterDelegate:nil
            userDriverDelegate:nil];
    }
}

void SparkleUpdater_CheckForUpdates() {
    if (updaterController != nil) {
        [updaterController checkForUpdates:nil];
    }
}

#endif // __APPLE__
