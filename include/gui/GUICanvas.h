/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   GUICanvas: Contains rendering and input functions for a page
*****************************************************************************/

#ifndef GUICANVAS_H_
#define GUICANVAS_H_

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <deque>
#include <chrono>
using namespace std;

class cmdPasteBlock;

#include "MainApp.h"
#include "klsGLCanvas.h"
#include "klsMiniMap.h"   // an inline setMinimap() below calls into it
#include "CircuitPage.h"
#include "GUICircuit.h"
#include "klsCollisionChecker.h"
#include "wireSegment.h"

class klsCommand;
class guiWire;

// GateState and WireState -- where a gate or wire was before a move -- live
// with the page, in CircuitPage.h.

// ConnectionSource lives with the page, in CircuitPage.h.

#include "commands.h"

#define MAX_UNDO_STATES 256

#define SCROLL_TIMER_RATE 30
#define SCROLL_TIMER_ID 1

// Zoom and pan constants live with CanvasCamera; the hotspot and drag dead
// zones, and DragState, live with CircuitPage.

// Engine-neutral rendering seam (Workstream G); defined in gui/render/.
namespace cl { namespace render { class Scene; struct RenderStyle; struct Transform; } }

// Class GUICanvas, inherits from klsGLCanvas for basic scroll/zoom/viewport functionality
//		all event handling is passed to this subclass in GL coordinates.
//		GUICanvas handles all gate and wire manipulation.
// The wx half is klsGLCanvas: window, GL context, event plumbing. The circuit
// half -- the gates and wires, how they draw, and what the pointer does to them
// -- is CircuitPage, which carries no toolkit, so the browser runs the same
// code. This class joins them, and answers PageHost on the page's behalf.
class GUICanvas: public klsGLCanvas, public CircuitPage, public PageHost
{
public:
    GUICanvas( wxWindow *parent, GUICircuit* gCircuit, wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0, const wxString& name = "GUICanvas" );

	virtual ~GUICanvas();

	// The interaction -- what a click selects, what starts a drag, where a wire
	// snaps -- lives on CircuitPage, so it is the same code in the browser. This
	// class is the surface it runs on: it answers PageHost from wxWidgets.
	GLPoint2f pointerPos() const override { return const_cast<GUICanvas *>(this)->getMouseCoords(); }
	GLPoint2f dragStart(input::Button b) const override {
		return const_cast<GUICanvas *>(this)->getDragStartCoords(toMouseButton(b));
	}
	GLPoint2f dragEnd(input::Button b) const override {
		return const_cast<GUICanvas *>(this)->getDragEndCoords(toMouseButton(b));
	}
	bool isDragging(input::Button b) const override {
		return const_cast<GUICanvas *>(this)->klsGLCanvas::isDragging(toMouseButton(b));
	}
	void beginDrag(input::Button b) override { klsGLCanvas::beginDrag(toMouseButton(b)); }
	void endDrag(input::Button b) override { klsGLCanvas::endDrag(toMouseButton(b)); }
	void requestRepaint() override { Refresh(); }
	void requestQuickAdd() override;
	void requestGateParams(unsigned long gateId) override;
	void setArrowCursor() override { SetCursor(wxCursor(wxCURSOR_ARROW)); }
	void setAutoScroll(bool on) override { on ? autoScrollEnable() : autoScrollDisable(); }
	bool isLocked() const override { return const_cast<GUICanvas *>(this)->klsGLCanvas::isLocked(); }

	// Rendering. These three forward to CircuitPage, supplying the grid spacing
	// and device scale this window knows and the page does not.
	void renderToScene(cl::render::Scene& scene, const cl::render::RenderStyle& style,
	                   int deviceW, int deviceH);
	void renderLiveToScene(cl::render::Scene& scene, const cl::render::RenderStyle& style);
	void drawSceneContents(cl::render::Scene& scene, const cl::render::RenderStyle& style,
	                       const cl::render::Transform& t, float scale,
	                       float gMinX, float gMinY, float gMaxX, float gMaxY);
	void drawGridInto(cl::render::Scene& scene, const cl::render::RenderStyle& style,
	                  float scale, float gMinX, float gMinY, float gMaxX, float gMaxY);
	bool renderSkiaLive() override;

	// Keep the minimap in step with the page as well.
	void updatePage() override;

	// The minimap that mirrors this canvas.
	void setMinimap(klsMiniMap* map) {
		minimap = map;
		if (minimap != NULL) {
			minimap->setCanvas(this);
			minimap->setLists(getGateList(), getWireList());
		}
	}

	void OnSize( void ) { updatePage(); };

	// Dump the page to the app log.
	void printLists();

private:
	static mouseButton toMouseButton(input::Button b) {
		switch (b) {
		case input::Button::Right:  return BUTTON_RIGHT;
		case input::Button::Middle: return BUTTON_MIDDLE;
		default:                    return BUTTON_LEFT;
		}
	}
};

#endif /*TESTGLCANVAS_H_*/
