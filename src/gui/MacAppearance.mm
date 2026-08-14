/*****************************************************************************
   Project: CEDAR Logic Simulator
   MacAppearance: pin macOS-native chrome to an app surface's own appearance
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "MacAppearance.h"

void MacForceLightAppearance(void* nsView) {
    if (nsView == NULL) return;
    NSView *view = (NSView *)nsView;
    // NSAppearance inherits down the view tree, so this reaches the NSScroller
    // wxWidgets parents to the window as well as the window itself.
    view.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
}

#endif // __APPLE__
