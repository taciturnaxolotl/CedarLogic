/*****************************************************************************
   Project: CEDAR Logic Simulator
   NativeIcons: macOS native SF Symbol toolbar icons
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "wx/bitmap.h"
#include "NativeIcons.h"

wxBitmap NativeIcon_GetSFSymbol(const char* symbolName, int pointSize) {
    @autoreleasepool {
        NSString *name = [NSString stringWithUTF8String:symbolName];
        NSImage *symbol = [NSImage imageWithSystemSymbolName:name
                                    accessibilityDescription:nil];
        if (!symbol) return wxNullBitmap;

        NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration
            configurationWithPointSize:(CGFloat)pointSize
            weight:NSFontWeightRegular
            scale:NSImageSymbolScaleMedium];
        symbol = [symbol imageWithSymbolConfiguration:config];

        return wxBitmap((__bridge WXImage)symbol);
    }
}

#endif // __APPLE__
