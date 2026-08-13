/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsMiniMap: Renders as a bitmap the whole circuit
*****************************************************************************/

#include "klsMiniMap.h"
#include "Settings.h"

#include "guiGate.h"
#include "guiWire.h"

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "render/Scene.h"
#include "render/RenderStyle.h"
#ifdef WITH_SKIA
#include "render/SkiaProbe.h"
#endif

// Enable access to objects in the main application
DECLARE_APP(MainApp)

BEGIN_EVENT_TABLE(klsMiniMap, wxPanel)
	EVT_PAINT(klsMiniMap::OnPaint)
	EVT_MOUSE_EVENTS(klsMiniMap::OnMouseEvent)
END_EVENT_TABLE()

klsMiniMap::klsMiniMap(wxWindow *parent, wxWindowID id,
        const wxPoint& pos, const wxSize& size,
        long style, const wxString& name)
		: wxGLCanvas(parent, glCanvasAttributes(), id, pos, size, style|wxSUNKEN_BORDER, name) {
	currentCanvas = NULL;
}

// Compute the fit box the thumbnail is drawn into (minCorner/maxCorner) and the
// content signature that caches it. Must run before generateImageSkia().
void klsMiniMap::setViewport() {
	wxSize sz = GetClientSize();
	float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
	// Accumulate a cheap content signature (gate ids + positions) so the Skia
	// path can cache the rendered thumbnail and skip the full redraw while the
	// circuit is unchanged -- e.g. while the user pans the main canvas.
	unsigned long long sig = 1469598103934665603ULL;  // FNV-ish seed
	auto mix = [&sig](unsigned long long v) { sig = (sig ^ v) * 1099511628211ULL; };
	auto mixf = [&mix](float f) { unsigned u; std::memcpy(&u, &f, sizeof u); mix(u); };
	unordered_map < unsigned long, guiGate* >::iterator gateWalk = gateList->begin();
	while (gateWalk != gateList->end()) {
		float x, y;
		(gateWalk->second)->getGLcoords(x, y);
		if (x < minX) minX = x;
		if (y < minY) minY = y;
		if (x > maxX) maxX = x;
		if (y > maxY) maxY = y;
		mix(gateWalk->first);
		mix(gateWalk->second->geometryHash());   // transform + params (rotation, label...)
		gateWalk++;
	}
	// Wire routing too, so reshaping a wire re-renders the thumbnail.
	if (wireList)
		for (auto& kv : *wireList)
			if (kv.second) { mix(kv.first); mix(kv.second->geometryHash()); }
	mix(gateList->size()); mix(wireList ? wireList->size() : 0);
	// Extend the fit to include the current viewport rectangle, so panning/zooming
	// the main canvas out past the circuit widens the thumbnail for navigation
	// (rather than capping to the drawing's bounds). While the viewport stays
	// within the circuit this is a no-op, so the thumbnail -- and its cache --
	// stay stable; the fit only changes once you move beyond the circuit. The
	// final fit is folded into contentSig below so the cache invalidates then.
	if (origin.x < minX) minX = origin.x;
	if (origin.y > maxY) maxY = origin.y;
	if (endpoint.x > maxX) maxX = endpoint.x;
	if (endpoint.y < minY) minY = endpoint.y;

	minCorner = GLPoint2f(minX-5,maxY+5);
	maxCorner = GLPoint2f(maxX+5,minY-5);

	double screenAspect = (double) sz.GetHeight() / (double) sz.GetWidth();
	double mapWidth = maxCorner.x - minCorner.x;
	double mapHeight = minCorner.y - maxCorner.y; // max and min corner's defs are weird...
	
	GLPoint2f orthoBoxTL, orthoBoxBR;
	
	// If the map's width is the limiting factor:
	if( screenAspect * mapWidth >= mapHeight ) {
		// Fit to width:
		double imageHeight = screenAspect * mapWidth;

		// Set the ortho box width equal to the map width, and center the
		// height in the box:
		orthoBoxTL = GLPoint2f( minCorner.x, minCorner.y + 0.5*(imageHeight - mapHeight) );
		orthoBoxBR = GLPoint2f( maxCorner.x, maxCorner.y - 0.5*(imageHeight - mapHeight) );
	} else {
		// Fit to height:
		double imageWidth = mapHeight / screenAspect;

		// Set the ortho box height equal to the map height, and center the
		// width in the box:
		orthoBoxTL = GLPoint2f( minCorner.x - 0.5*(imageWidth - mapWidth), minCorner.y );
		orthoBoxBR = GLPoint2f( maxCorner.x + 0.5*(imageWidth - mapWidth), maxCorner.y );
	}

	// Store minCorner and maxCorner for use in the Skia render + mouse handler:
	minCorner = orthoBoxTL;
	maxCorner = orthoBoxBR;

	// Fold the final fit into the cache key: the thumbnail re-renders when the fit
	// changes (viewport moved beyond the circuit and widened it) but stays cached
	// while the fit is stable (viewport within the circuit -- the common case).
	mixf(minCorner.x); mixf(minCorner.y); mixf(maxCorner.x); mixf(maxCorner.y);
	// Connection-dot settings affect the thumbnail too.
	mix(appConfig().appSettings.wireConnVisible ? 1u : 0u);
	mixf((float)appConfig().appSettings.wireConnRadius);
	contentSig = sig;
}

// Render the whole circuit through Skia, via the same Scene seam the main
// canvas uses.
#ifdef WITH_SKIA
bool klsMiniMap::generateImageSkia() {
	using namespace cl::render;
	wxSize sz = GetClientSize();
	const double sf = GetContentScaleFactor();
	const int w = (int)(sz.GetWidth() * sf), h = (int)(sz.GetHeight() * sf);
	const double spanX = maxCorner.x - minCorner.x;   // fit box from setViewport()
	if (w <= 0 || h <= 0 || spanX <= 1e-6) return false;

	// world -> device: x'=(x-minCorner.x)*scale ; y'=(minCorner.y-y)*scale, where
	// minCorner is the fit box's top-left (setViewport stores orthoBoxTL there).
	const float scale = (float)(w / spanX);
	Transform t;
	t.a = scale;  t.c = 0; t.e = (float)(-minCorner.x * scale);
	t.b = 0; t.d = -scale; t.f = (float)( minCorner.y * scale);

	klsMiniMap* self = this;
	RenderStyle style = RenderStyle::print();   // black outlines, no grid, no live state
	// Hairline strokes: the whole circuit is shrunk to a thumbnail, so full-weight
	// lines would collapse dense clusters into a black blob.
	const float strokeScale = 0.5f;

	// Static layer: the circuit thumbnail. Cached by contentSig, so panning the
	// main canvas (which only moves the viewport rect) re-blits instead of
	// re-running drawToScene over every gate/wire.
	auto drawCircuit = [self, &style, &t](Scene& scene) {
		scene.setViewport(t);
		if (self->wireList)
			for (auto& kv : *self->wireList)
				if (kv.second) kv.second->drawToScene(scene, style);
		if (self->gateList)
			for (auto& kv : *self->gateList)
				if (kv.second) kv.second->drawToScene(scene, style);
	};
	// Overlay: the red rectangle marking the main canvas's visible area (moves
	// every pan, always redrawn). The fit includes the viewport, so the rect is
	// always within the thumbnail -- draw it directly.
	auto drawViewportRect = [self, &t](Scene& scene) {
		if (!self->gateList || self->gateList->empty()) return;
		scene.setViewport(t);
		const Point box[] = { Point((float)self->origin.x,   (float)self->origin.y),
		                      Point((float)self->origin.x,   (float)self->endpoint.y),
		                      Point((float)self->endpoint.x, (float)self->endpoint.y),
		                      Point((float)self->endpoint.x, (float)self->origin.y) };
		scene.polyline(box, 4, Stroke(Color(1.0f, 0.0f, 0.0f, 1.0f), 2.0f), true);
	};

	const bool ok = skiaRenderWindowCached(w, h, 0, contentSig,
	                                       drawCircuit, drawViewportRect, strokeScale);
	if (ok) SwapBuffers();
	return ok;
}
#endif

void klsMiniMap::update(GLPoint2f origin, GLPoint2f endpoint) {
	this->origin = origin;
	this->endpoint = endpoint;
	Refresh();
}

void klsMiniMap::OnPaint(wxPaintEvent& evt) {
	wxGetApp().SetCurrentCanvas(this);
	// setViewport() computes the fit box (minCorner/maxCorner) the Skia render and
	// the mouse handler both read; it must run before generateImageSkia().
	setViewport();
	generateImageSkia();
}

void klsMiniMap::OnMouseEvent(wxMouseEvent& evt) {
	if (evt.LeftIsDown() && currentCanvas != NULL) {
		GLPoint2f p1, p2;
		wxSize sz = GetClientSize();
		currentCanvas->getViewport( p1, p2 );
		GLPoint2f halfViewport((p2.x-p1.x)/2, (p2.y-p1.y)/2);
		int diffX = evt.GetPosition().x, diffY = evt.GetPosition().y;
		float fdiffX = diffX * (maxCorner.x-minCorner.x)/sz.GetWidth();
		float fdiffY = diffY * (maxCorner.y-minCorner.y)/sz.GetHeight();
		currentCanvas->setPan( minCorner.x+fdiffX-halfViewport.x, minCorner.y+fdiffY-halfViewport.y );
	}
}
