/*****************************************************************************
   Project: CEDAR Logic Simulator
   NativeIcons: macOS native SF Symbol toolbar icons
*****************************************************************************/

#ifndef NATIVEICONS_H
#define NATIVEICONS_H

#ifdef __APPLE__

#include "wx/bitmap.h"

// Returns a wxBitmap created from a macOS SF Symbol (requires macOS 11+).
// Returns wxNullBitmap if the symbol name is not found.
wxBitmap NativeIcon_GetSFSymbol(const char* symbolName, int pointSize);

#endif // __APPLE__

#endif // NATIVEICONS_H
