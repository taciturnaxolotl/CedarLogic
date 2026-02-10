/*****************************************************************************
   Project: CEDAR Logic Simulator
   NativeIcons: macOS native SF Symbol toolbar icons
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "wx/bitmap.h"
#include "wx/toolbar.h"
#include "wx/frame.h"
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

void NativeIcon_SetToolbarSFSymbol(wxToolBar* toolbar, int toolId,
                                    const char* symbolName, int pointSize) {
    @autoreleasepool {
        NSString *name = [NSString stringWithUTF8String:symbolName];
        NSImage *symbol = [NSImage imageWithSystemSymbolName:name
                                    accessibilityDescription:nil];
        if (!symbol) return;

        NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration
            configurationWithPointSize:(CGFloat)pointSize
            weight:NSFontWeightRegular
            scale:NSImageSymbolScaleMedium];
        symbol = [symbol imageWithSymbolConfiguration:config];

        // wxWidgets uses the wxToolBarTool pointer address (as a string) for
        // the NSToolbarItem identifier. Find the tool, format its address,
        // and match against NSToolbar items.
        wxToolBarToolBase* tool = toolbar->FindById(toolId);
        if (!tool) return;

        NSString *identifier = [NSString stringWithFormat:@"%ld", (long)tool];

        NSWindow *window = [((NSView*)toolbar->GetHandle()) window];
        NSToolbar *nstoolbar = [window toolbar];
        if (!nstoolbar) return;

        for (NSToolbarItem *item in [nstoolbar items]) {
            if ([[item itemIdentifier] isEqualToString:identifier]) {
                [item setImage:symbol];
                return;
            }
        }
    }
}

void NativeWindow_ConfigureTitleBar(wxFrame* frame) {
    @autoreleasepool {
        NSView* view = (NSView*)frame->GetHandle();
        if (!view) return;

        NSWindow *window = [view window];
        if (!window) return;

        // Merge the title bar and toolbar into one unified area
        window.styleMask |= NSWindowStyleMaskUnifiedTitleAndToolbar;

        // Keep the title visible alongside toolbar items
        window.titleVisibility = NSWindowTitleVisible;

        // Use the modern unified toolbar style (macOS 11+)
        if (@available(macOS 11.0, *)) {
            window.toolbarStyle = NSWindowToolbarStyleUnified;
        }
    }
}

#endif // __APPLE__
