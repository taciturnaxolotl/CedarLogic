/*****************************************************************************
   Project: CEDAR Logic Simulator
   MacAppearance: pin macOS-native chrome to an app surface's own appearance
*****************************************************************************/

#ifndef MACAPPEARANCE_H
#define MACAPPEARANCE_H

#ifdef __APPLE__

// Force `nsView` (from wxWindow::GetHandle()) and everything nested inside it to
// the light appearance, whatever the system is set to. For panels the app paints
// light itself: their native chrome -- scrollbars above all -- otherwise inherits
// the system appearance and renders for a dark background that isn't there.
void MacForceLightAppearance(void* nsView);

#endif // __APPLE__

#endif // MACAPPEARANCE_H
