/*****************************************************************************
   Project: CEDAR Logic Simulator

   ToolbarIcons: the SVG toolbar icons, tinted to suit the bar they sit on.

   The icons in res/icons are Lucide outlines drawn with a literal #333333
   stroke, so something has to recolor them before they can appear on a dark
   toolbar. The colour is chosen from the toolbar's own background rather than
   from the system theme preference: wx 3.2 does not theme native MSW controls,
   so a machine set to dark keeps a light toolbar, and trusting the preference
   there paints near-white icons onto a near-white bar.

   macOS does not come through here at all; it draws SF Symbols, which AppKit
   tints on its own (see NativeIcons.h).
*****************************************************************************/

#pragma once

#include <wx/bmpbndl.h>
#include <wx/gdicmn.h>

class wxWindow;

namespace cl {

// The named icon under res/icons (without the .svg), sized for `size` and
// tinted to contrast with `bar`. Falls back to a question mark if the resource
// is missing or will not parse, so a bad name costs an odd-looking button
// rather than an invisible one.
wxBitmapBundle toolbarIcon(const wxWindow* bar, const char* name, wxSize size);

}  // namespace cl
