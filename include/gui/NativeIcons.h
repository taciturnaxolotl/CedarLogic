/*****************************************************************************
   Project: CEDAR Logic Simulator
   NativeIcons: macOS native SF Symbol toolbar icons
*****************************************************************************/

#ifndef NATIVEICONS_H
#define NATIVEICONS_H

#ifdef __APPLE__

#include "wx/bitmap.h"
#include "wx/frame.h"

// Returns a wxBitmap created from a macOS SF Symbol (requires macOS 11+).
// Returns wxNullBitmap if the symbol name is not found.
wxBitmap NativeIcon_GetSFSymbol(const char* symbolName, int pointSize);

// Updates a toolbar item's image to an SF Symbol, bypassing wxWidgets'
// broken alternate-image generation for toggle tools.
void NativeIcon_SetToolbarSFSymbol(wxToolBar* toolbar, int toolId,
                                    const char* symbolName, int pointSize);

// Configures the NSWindow for a modern unified title bar + toolbar appearance.
// Requires macOS 11+ for full effect; degrades gracefully on older versions.
void NativeWindow_ConfigureTitleBar(wxFrame* frame);

#endif // __APPLE__

#endif // NATIVEICONS_H
