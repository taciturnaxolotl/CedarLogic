/*****************************************************************************
   Project: CEDAR Logic Simulator

   CircuitPage: the contents of one page -- its gates, its wires, and how they
   draw -- with no toolkit attached.
*****************************************************************************/

// GUICanvas is two things wearing one coat: a wxGLCanvas that handles mouse and
// keyboard, and the circuit page it happens to be showing. Only the first half
// needs wxWidgets. This class is the second half: the gate and wire lists, and
// the methods that walk them into an engine-neutral cl::render::Scene.
//
// Splitting it lets the page render anywhere a Scene backend exists -- Skia on
// the desktop, Canvas2D in the browser -- from the same geometry code, so the
// two never drift. GUICanvas inherits it and keeps its old method signatures,
// feeding in the grid spacing it holds as a klsGLCanvas.

#ifndef CIRCUITPAGE_H_
#define CIRCUITPAGE_H_

#include <unordered_map>

#include <map>

#include "klsCollisionChecker.h"
#include "wireSegment.h"

#include "gl_defs.h"   // GRID_INTENSITY, MIN_GRID_SCREEN_SPACING
#include "CanvasCamera.h"

namespace cl { namespace render {
	class Scene;
	struct RenderStyle;
	struct Transform;
} }

class guiGate;
class guiWire;
class GUICircuit;

// Where a gate was before a move started, so the move can be undone and so a
// drag can be measured against its origin.
struct GateState {
	GateState( unsigned int nID, float nX, float nY, bool nSel ) : id(nID), x(nX), y(nY), selected(nSel) {}
	unsigned int id;
	float x;
	float y;
	bool selected;
};

// The same for a wire, which also has to remember the shape of its segment tree:
// a move can reroute it, and undo has to put the old route back.
struct WireState {
	WireState( unsigned int nID, GLPoint2f nPoint, std::map< long, wireSegment > nTree ) :
		id(nID), point(nPoint), oldWireTree(nTree) {}
	unsigned int id;
	GLPoint2f point;
	std::map< long, wireSegment > oldWireTree;
};

class CircuitPage {
public:
	virtual ~CircuitPage() {}

	std::unordered_map< unsigned long, guiGate* > gateList;
	std::unordered_map< unsigned long, guiWire* > wireList;

	std::unordered_map< unsigned long, guiGate* >* getGateList() { return &gateList; }
	std::unordered_map< unsigned long, guiWire* >* getWireList() { return &wireList; }

	// The camera looking at this page. Owned by whoever owns the surface -- the
	// canvas on the desktop, the document in the browser -- because a camera
	// needs a viewport and only they know how big it is.
	CanvasCamera* getCamera() const { return pageCamera; }
	void setCamera(CanvasCamera* c) { pageCamera = c; }

	// The visible world rectangle, which the legacy save format records per
	// page. Empty if no camera is attached.
	void getViewport(GLPoint2f& topLeft, GLPoint2f& bottomRight) const {
		if (pageCamera) pageCamera->getViewport(topLeft, bottomRight);
	}

	// The document these gates belong to. The page holds the objects; the
	// document owns their logic-core counterparts and the undo history.
	GUICircuit* getCircuit() const { return gCircuit; }
	void setCircuit(GUICircuit* circuit) { gCircuit = circuit; }

	// Everything on the page, indexed for hit testing and overlap.
	klsCollisionChecker& getCollisionChecker() { return collisionChecker; }

	// Put an existing gate on the page at a world position.
	void insertGate(unsigned long id, guiGate* gate, float x, float y);

	// Put an existing wire on the page. A bus wire claims several ids; all of
	// them are reserved so nothing else takes one.
	void insertWire(guiWire* wire);

	// Take them off again. Both are no-ops if the object is not on this page.
	void removeGate(unsigned long id);
	void removeWire(unsigned long id);

	// Drop every gate and wire. Does not touch interaction state -- the shell
	// clears its own.
	void clearPage();

	// Fit the whole circuit into a deviceW x deviceH image and draw it. This is
	// the export path (PNG/SVG/PDF and the headless --render flag); the live
	// canvas computes its own camera and calls drawSceneContents directly.
	void renderToScene(cl::render::Scene& scene, const cl::render::RenderStyle& style,
	                   int deviceW, int deviceH, float horizSpacing, float vertSpacing);

	// Draw at the camera's live pan and zoom, rather than fitting the circuit to
	// the image. This is the on-screen path. `contentScale` is device pixels per
	// logical pixel, so a HiDPI display draws sharp instead of doubled.
	void renderLiveToScene(cl::render::Scene& scene, const cl::render::RenderStyle& style,
	                       const CanvasCamera& camera, float contentScale);

	// Grid, then wires, then gates, under an already-computed viewport.
	void drawSceneContents(cl::render::Scene& scene, const cl::render::RenderStyle& style,
	                       const cl::render::Transform& t, float scale,
	                       float horizSpacing, float vertSpacing,
	                       float gMinX, float gMinY, float gMaxX, float gMaxY);

	void drawGridInto(cl::render::Scene& scene, const cl::render::RenderStyle& style,
	                  float scale, float horizSpacing, float vertSpacing,
	                  float gMinX, float gMinY, float gMaxX, float gMaxY);

	void drawCircuitInto(cl::render::Scene& scene, const cl::render::RenderStyle& style);

	// A cheap signature of everything that affects the rendered circuit, used to
	// decide whether a retained scene can be replayed instead of re-recorded.
	unsigned long long renderContentKey();

	// Called when a gate leaves the page, so a shell tracking it (a hovered pin,
	// say) can let go. Default does nothing.
	virtual void onGateRemoved(unsigned long id) { (void)id; }

protected:
	klsCollisionChecker collisionChecker;
	GUICircuit* gCircuit = nullptr;
	CanvasCamera* pageCamera = nullptr;
};

#endif /*CIRCUITPAGE_H_*/
