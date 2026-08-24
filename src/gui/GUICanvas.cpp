/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   GUICanvas: Contains rendering and input functions for a page
*****************************************************************************/

#include "GUICanvas.h"
#include "PaletteDrag.h"
#include "RenderMode.h"
#include "Settings.h"
#include "GateLibrary.h"
#include "MainApp.h"
#include "paramDialog.h"
#include "QuickAddDialog.h"
#include "klsClipboard.h"
#include "guiWire.h"
#include "render/Scene.h"
#include "render/RenderStyle.h"
#ifdef WITH_SKIA
#include "render/SkiaProbe.h"
#endif


#include <wx/dnd.h>
#include <cstring>

// Included to use the min() and max() templates:
#include <algorithm>
#include <iostream>
using namespace std;

// Enable access to objects in the main application
DECLARE_APP(MainApp)

unsigned int renderTime = 0;
unsigned int renderNum = 0;

// TODO: this should probably get it's own file
class DnDText : public wxTextDropTarget {
public:
	DnDText(GUICanvas* canvas) { m_canvas = canvas; }

	bool OnDropText(wxCoord x, wxCoord y, const wxString& text) wxOVERRIDE {
		string gateName = text.ToStdString();

		// Make sure the gate exists
		if (gateLibrary().gateNameToLibrary.count(gateName) == 0) {
			return false;
		}

		wxPoint m(x, y);
		m_canvas->addGate(gateName, m_canvas->mapToCanvas(m));
		return true;
	};

private:
	GUICanvas* m_canvas;
};

// GUICanvas constructor - defaults grid size to 1 unit square
GUICanvas::GUICanvas(wxWindow *parent, GUICircuit* gCircuit, wxWindowID id,
    const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : klsGLCanvas(parent, name, id, pos, size, style|wxSUNKEN_BORDER ) {
	// The page and the window share one camera and one document, so nothing can
	// drift between what is shown and what is drawn.
	CircuitPage::setCamera(&canvasCamera());
	CircuitPage::setCircuit(gCircuit);
	// This window is the surface the page's interaction runs on.
	CircuitPage::setHost(this);

	setHorizGrid(0.5);
	setVertGrid(0.5);
	
	SetDropTarget(new DnDText(this));

#ifdef __WXOSX__
	// Suppress macOS bonk sound for keys handled in OnKeyDown
	Bind(wxEVT_CHAR, [](wxKeyEvent& evt) {
		int key = evt.GetKeyCode();
		if (key == 'a' || key == 'A' || key == 'r' || key == 'R' ||
			key == WXK_SPACE || key == '+' || key == '=' || key == '-') {
			// Swallow — already handled in OnKeyDown
		} else {
			evt.Skip();
		}
	});
#endif
}

GUICanvas::~GUICanvas() {}








// Render the page
// Render the whole page into the engine-neutral Scene (Workstream G). Fits the
// circuit's world bounding box into a deviceW x deviceH viewport (y-flipped for a
// top-left device origin), then emits every wire and gate.
// The page-drawing methods now live on CircuitPage (wx-free, so the browser
// build shares them). These forward the grid spacing GUICanvas holds as a
// klsGLCanvas, keeping every existing call site unchanged.
void GUICanvas::renderToScene(cl::render::Scene& scene,
                              const cl::render::RenderStyle& style,
                              int deviceW, int deviceH) {
	CircuitPage::renderToScene(scene, style, deviceW, deviceH,
	                           canvasCamera().horizSpacing(), canvasCamera().vertSpacing());
}

void GUICanvas::drawGridInto(cl::render::Scene& scene,
                             const cl::render::RenderStyle& style, float scale,
                             float gMinX, float gMinY, float gMaxX, float gMaxY) {
	CircuitPage::drawGridInto(scene, style, scale, canvasCamera().horizSpacing(), canvasCamera().vertSpacing(),
	                          gMinX, gMinY, gMaxX, gMaxY);
}

void GUICanvas::drawSceneContents(cl::render::Scene& scene,
                                  const cl::render::RenderStyle& style,
                                  const cl::render::Transform& t, float scale,
                                  float gMinX, float gMinY,
                                  float gMaxX, float gMaxY) {
	CircuitPage::drawSceneContents(scene, style, t, scale,
	                               canvasCamera().horizSpacing(), canvasCamera().vertSpacing(),
	                               gMinX, gMinY, gMaxX, gMaxY);
}

// Render the page at the LIVE camera (pan/zoom), not the bbox fit -- this is the
// on-screen path (G3). The camera is the canvas's own pan/zoom:
//   world x in [panX, panX + w*viewZoom], y in [panY - h*viewZoom, panY], mapped
//   to physical pixels. So device px per world unit = contentScale / viewZoom,
//   and world y is flipped for the top-left device origin.
void GUICanvas::renderLiveToScene(cl::render::Scene& scene,
                                  const cl::render::RenderStyle& style) {
	CircuitPage::renderLiveToScene(scene, style, canvasCamera(),
	                               (float)GetContentScaleFactor());
}

// G3: paint the live frame through Skia's Ganesh backend into the window FBO.
// The grid is drawn live (camera-dependent); the circuit is retained in an
// SkPicture and replayed under the camera, so a pan is a cheap replay.
bool GUICanvas::renderSkiaLive() {
#ifdef WITH_SKIA
	using namespace cl::render;
	wxSize sz = GetClientSize();
	const double sf = GetContentScaleFactor();
	const int w = (int)(sz.GetWidth() * sf), h = (int)(sz.GetHeight() * sf);
	if (w <= 0 || h <= 0) return false;
	GLdouble px, py; getPan(px, py);
	double vz = getZoom();
	if (vz <= 0) vz = 1.0;
	const float scale = (float)(sf / vz);
	Transform t;
	t.a = scale;  t.c = 0; t.e = (float)(-px * scale);
	t.b = 0; t.d = -scale; t.f = (float)( py * scale);
	const float gMinX = (float)px;
	const float gMaxX = (float)(px + sz.GetWidth()  * vz);
	const float gMinY = (float)(py - sz.GetHeight() * vz);
	const float gMaxY = (float)py;

	GUICanvas* self = this;
	RenderStyle style = RenderStyle::screen();
	// View > Display Gridlines. The screen style defaults the grid on and the
	// export paths set showGrid themselves, so without this the live canvas was
	// the one renderer that never consulted the setting.
	style.showGrid = appConfig().appSettings.gridlineVisible;
	// Key the cached circuit picture on the circuit CONTENT only. The interactive
	// overlays (hover bulb, drag box, wire hover, ...) are drawn live on top each
	// frame via drawOverlay below, so they follow the mouse WITHOUT invalidating
	// the picture -- otherwise every mouse move re-recorded the whole scene, which
	// is what let fast mouse movement starve the sim (see perf notes).
	unsigned long long sceneKey = renderContentKey();
	auto drawGrid = [self, style, t, scale, gMinX, gMinY, gMaxX, gMaxY](Scene& s) {
		s.setViewport(t);
		self->drawGridInto(s, style, scale, gMinX, gMinY, gMaxX, gMaxY);
	};
	auto drawScene = [self, style](Scene& s) {
		self->drawCircuitInto(s, style);
	};
	auto drawOverlay = [self, t](Scene& s) {
		s.setViewport(t);
		self->drawOverlaysInto(s);
	};
	return skiaRenderWindowScene(w, h, 0, sceneKey, t, drawGrid, drawScene, drawOverlay);
#else
	return false;
#endif
}

#ifdef WITH_SKIA
#endif










void GUICanvas::requestQuickAdd() {
#ifdef __WXOSX__
	QuickAddDialog* dlg = new QuickAddDialog(wxTheApp->GetTopWindow());
	dlg->Bind(wxEVT_WINDOW_MODAL_DIALOG_CLOSED, [dlg](wxWindowModalDialogEvent& evt) {
		if (evt.GetReturnCode() == wxID_OK && !dlg->getSelectedGate().empty()) {
			paletteDrag().newGateToDrag = dlg->getSelectedGate();
		}
		dlg->Destroy();
	});
	dlg->ShowWindowModal();
#else
	QuickAddDialog dlg(wxGetTopLevelParent(this));
	if (dlg.ShowModal() == wxID_OK && !dlg.getSelectedGate().empty()) {
		paletteDrag().newGateToDrag = dlg.getSelectedGate();
		CallAfter([this]() { SetFocus(); });
	}
#endif
}









// print page contents
void GUICanvas::printLists() {
	wxGetApp().logfile << "printing page lists" << endl << flush;
	unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		float x, y;
		(thisGate->second)->getGLcoords(x, y);
		wxGetApp().logfile << " gate " << thisGate->first << " type " << (thisGate->second)->getLibraryGateName() << " at " << x << "," << y << endl << flush;
		thisGate++;
	}
	unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		wxGetApp().logfile << " wire " << thisWire->first << endl << flush;
		thisWire++;
	}
}	


//Julian: Moved implementation of zoom fuctions out of header.








// The page's own update plus the minimap, which mirrors this canvas and has to
// be told when its contents move.
void GUICanvas::updatePage() {
	CircuitPage::updatePage();
	if (minimap == NULL) return;
	minimap->setLists(getGateList(), getWireList());
	minimap->setCanvas(this);
	updateMiniMap();
	wxWindow::Update();
}
