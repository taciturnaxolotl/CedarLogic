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
#include "guiText.h"

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
		: wxGLCanvas(parent, id, NULL, pos, size, style|wxSUNKEN_BORDER, name) {
	currentCanvas = NULL;
}

void klsMiniMap::setViewport() {
	// Set the projection matrix:	
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();

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

	// gluOrtho2D(left, right, bottom, top); (In world-space coords.)
	gluOrtho2D(orthoBoxTL.x, orthoBoxBR.x, orthoBoxBR.y, orthoBoxTL.y);
	// Use physical pixels for glViewport on HiDPI/Retina displays
	double scaleFactor = GetContentScaleFactor();
	glViewport(0, 0, (GLint)(sz.GetWidth() * scaleFactor), (GLint)(sz.GetHeight() * scaleFactor));

	// Store minCorner and maxCorner for use in mouse handler:
	minCorner = orthoBoxTL;
	maxCorner = orthoBoxBR;

	// Fold the final fit into the cache key: the thumbnail re-renders when the fit
	// changes (viewport moved beyond the circuit and widened it) but stays cached
	// while the fit is stable (viewport within the circuit -- the common case).
	mixf(minCorner.x); mixf(minCorner.y); mixf(maxCorner.x); mixf(maxCorner.y);
	contentSig = sig;

	// Set the model matrix:
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();
}

// Render the whole circuit through Skia (G3), mirroring the GL renderMap() but
// via the same Scene seam the main canvas uses -- so the minimap migrates off
// fixed-function GL instead of fighting Skia for the shared GL context's state.
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
	RenderStyle style = RenderStyle::print();   // black outlines, no grid/live (= draw(false))
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

// Print the canvas contents to a bitmap:
void klsMiniMap::generateImage() {
	wxSize sz = GetClientSize();

	// Setup the viewport for rendering (also stores minCorner/maxCorner, used by
	// the Skia path below and the mouse handler):
	setViewport();

#ifdef WITH_SKIA
	if (appConfig().appSettings.useSkiaRenderer && generateImageSkia()) return;
#endif
	// Reset the glViewport to the size of the bitmap:
	double scaleFactorImg = GetContentScaleFactor();
	glViewport(0, 0, (GLint)(sz.GetWidth() * scaleFactorImg), (GLint)(sz.GetHeight() * scaleFactorImg));
	
	// Set the bitmap clear color:
	glClearColor (1.0, 1.0, 1.0, 0.0);
	glColor3b(0, 0, 0);
		
	//TODO: Check if alpha is hardware supported, and
	// don't enable it if not!
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	
	//*********************************
	//Edit by Joshua Lansford 4/08/07
	//The minimap could use some anti
	//aleasing
	glEnable( GL_LINE_SMOOTH );
	//End of edit
		
	// Load the font texture
	guiText::loadFont(appConfig().appSettings.textFontFile);

	// Do the rendering here.
	renderMap();

	// Flush the OpenGL buffer to make sure the rendering has happened:	
	glFlush();
	SwapBuffers();
}

void klsMiniMap::renderMap() {
	int w, h;
	GetClientSize(&w, &h);

	//clear window
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();
	glColor4f( 0, 0, 0, 1 );
	
	// Draw the wires:
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();
	unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList->begin();
	while( thisWire != wireList->end() ) {
		if (thisWire->second != nullptr) {
			(thisWire->second)->draw(false);
		}
		thisWire++;
	}

	// Draw the gates:
	unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList->begin();
	while( thisGate != gateList->end() ) {
		(thisGate->second)->draw(false);
		thisGate++;
	}
	
	if (gateList->size() == 0) return;
	glLoadIdentity();
	glColor4f( 1, 0, 0, 1 );
	GLfloat lineWidthOld;
	glGetFloatv(GL_LINE_WIDTH, &lineWidthOld);
	glLineWidth(2.0);
	glBegin(GL_LINE_LOOP);
		glVertex2f( origin.x, origin.y );
		glVertex2f( origin.x, endpoint.y );
		glVertex2f( endpoint.x, endpoint.y );
		glVertex2f( endpoint.x, origin.y );
	glEnd();
	glLineWidth(lineWidthOld);
}

void klsMiniMap::update(GLPoint2f origin, GLPoint2f endpoint) {
	this->origin = origin;
	this->endpoint = endpoint;
	Refresh();
}

void klsMiniMap::OnPaint(wxPaintEvent& evt) {
	wxGetApp().SetCurrentCanvas(this);
	generateImage();
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
