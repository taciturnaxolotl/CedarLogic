/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   ToolbarIcons: the image behind a toolbar button, on whichever platform.
*****************************************************************************/

#include "ToolbarIcons.h"

#include <string>

#include <wx/artprov.h>
#include <wx/buffer.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/window.h>

#include "EmbeddedRes.h"

#ifdef __APPLE__
#include "NativeIcons.h"
#endif

namespace cl {

namespace {

#ifndef __APPLE__

// The stroke every icon in res/icons is drawn with, and its light counterpart.
const char* const kAuthoredStroke = "#333333";
const char* const kLightStroke = "#E6E6E6";

// The colour an icon has to stand out against. Asking the toolbar beats asking
// wxSystemSettings::GetAppearance().IsDark(): that reports what the user picked
// in the OS, but wx 3.2 leaves native MSW controls light no matter what the
// user picked, so on a dark-mode Windows box the two disagree and the icons
// come out near-white on a near-white bar.
wxColour barBackground(const wxWindow* bar) {
	if (bar != nullptr) {
		const wxColour bg = bar->GetBackgroundColour();
		if (bg.IsOk()) return bg;
	}
	return wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
}

#endif  // !__APPLE__

}  // namespace

wxBitmapBundle toolbarIcon(const wxWindow* bar,
                           const char* sfSymbol, int sfSymbolPoints,
                           const char* svgName, wxSize svgSize) {
#ifdef __APPLE__
	// AppKit tints and scales SF Symbols itself, so nothing below applies.
	const wxBitmap symbol = NativeIcon_GetSFSymbol(sfSymbol, sfSymbolPoints);
	if (symbol.IsOk()) return symbol;
#else
	wxString svg = res::text(("icons/" + std::string(svgName) + ".svg").c_str());
	if (!svg.empty()) {
		const bool onDark = barBackground(bar).GetLuminance() < 0.5;
		svg.Replace(kAuthoredStroke, onDark ? kLightStroke : kAuthoredStroke);
		// FromSVG takes a mutable buffer (nanosvg parses it in place).
		wxScopedCharBuffer buf = svg.utf8_str();
		wxBitmapBundle b = wxBitmapBundle::FromSVG(buf.data(), svgSize);
		if (b.IsOk()) return b;
	}
#endif
	return wxBitmapBundle(wxArtProvider::GetBitmap(wxART_QUESTION, wxART_TOOLBAR));
}

}  // namespace cl
