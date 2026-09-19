/*****************************************************************************
   Project: CEDAR Logic Simulator

   ToolbarIcons: the image behind a toolbar button, on whichever platform.

   macOS draws SF Symbols, which AppKit tints and scales on its own (see
   NativeIcons.h). Everywhere else draws the Lucide outlines in res/icons. Those
   are authored with a literal #333333 stroke, so something has to recolor them
   before they can appear on a dark toolbar. The colour is chosen from the
   toolbar's own background rather than from the system theme preference: wx 3.2
   does not theme native MSW controls, so a machine set to dark keeps a light
   toolbar, and trusting the preference there paints near-white icons onto a
   near-white bar.

   The two platforms have separate vocabularies -- "doc.badge.plus" is Apple's
   word for what res/icons calls "new" -- so a caller names a button both ways
   and this picks. That is the whole point of the thing: it lets a toolbar be
   described once, in one list, instead of twice on either side of an #ifdef.
*****************************************************************************/

#pragma once

#include <wx/bmpbndl.h>
#include <wx/gdicmn.h>

class wxWindow;

namespace cl {

// The icon for a toolbar button: the named SF Symbol at `sfSymbolPoints` on
// macOS, or res/icons/<svgName>.svg drawn for `svgSize` and tinted to contrast
// with `bar` everywhere else. The sizes differ per platform because the two
// icon sets carry different optical weight, so each toolbar tunes them apart.
//
// `svgSize` is the size the art is drawn for, not a ceiling; the bundle
// rasterizes itself to whatever the display turns out to want.
//
// Falls back to a question mark if the icon is missing or will not parse, so a
// bad name costs an odd-looking button rather than an invisible one.
wxBitmapBundle toolbarIcon(const wxWindow* bar,
                           const char* sfSymbol, int sfSymbolPoints,
                           const char* svgName, wxSize svgSize);

}  // namespace cl
