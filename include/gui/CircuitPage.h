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

#include <chrono>
#include <string>
#include <vector>

#include "InputEvent.h"
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
class klsCommand;
class cmdPasteBlock;

// Where a drag-to-connect started.
struct ConnectionSource {
	bool isGate = false;
	unsigned long objectID = 0;
	std::string connection;

	ConnectionSource() {}
	ConnectionSource(bool ig, unsigned long id, std::string conn)
		: isGate(ig), objectID(id), connection(conn) {}
};

// The area that reacts as a hotspot, and the dead zones that separate a click
// from a drag. All in screen pixels, scaled by zoom at the point of use.
#define HOTSPOT_SCREEN_RADIUS 3.0
#define HOTSPOT_SCREEN_DELTA  5.0
#define WIRE_HOVER_SCREEN_DELTA 5.0
#define MOUSE_HOVER_DELTA 4.5
// Click-vs-drag dead zone: the pointer must leave this radius around the press
// point before a selected gate starts moving, so a click with a little jitter
// selects instead of nudging it a grid cell over.
#define DRAG_START_SCREEN_DELTA 6.0
// ... and a short time dead zone after the press, so a quick click that travels
// a few pixels while the button is down still selects rather than nudging.
#define DRAG_START_TIME_MS 85

#define ZOOM_ALL_MARGIN 0.25

// What the page is in the middle of doing with the pointer.
enum DragState {
	DRAG_NONE = 0,
	DRAG_CONNECT,
	DRAG_SELECT,
	DRAG_SELECTION,
	DRAG_NEWGATE,
	DRAG_WIRESEG
};

// What a page needs from whatever surface is showing it.
//
// The page decides what a click selects and what starts a drag; it cannot know
// where the pointer is in pixels, when a repaint is cheap, or how to open a
// window. Those are the surface's, and this is the whole of what the page asks
// for. GUICanvas answers from wxWidgets; the browser document answers from DOM
// events.
class PageHost {
public:
	virtual ~PageHost() {}

	// Where the pointer is, in world coordinates.
	virtual GLPoint2f pointerPos() const = 0;

	// Where a drag with this button began, and whether one is under way.
	virtual GLPoint2f dragStart(input::Button button) const = 0;
	virtual bool isDragging(input::Button button) const = 0;
	virtual void beginDrag(input::Button button) = 0;
	virtual void endDrag(input::Button button) = 0;

	// Something changed; repaint when convenient.
	virtual void requestRepaint() = 0;

	// Ask for a gate to place ('a'). Opening a window is the one thing a key
	// handler cannot do itself, so it asks: the desktop answers with a dialog,
	// the browser with its own picker. The answer lands in
	// paletteDrag().newGateToDrag.
	virtual void requestQuickAdd() {}

	// Put the cursor back to an arrow after a drag.
	virtual void setArrowCursor() {}

	// Scroll the view when a drag reaches the edge.
	virtual void setAutoScroll(bool on) { (void)on; }

	// Editing disabled (the desktop locks the canvas while the sim is paused
	// mid-step).
	virtual bool isLocked() const { return false; }
};

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
	CircuitPage();
	virtual ~CircuitPage();

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

	// --- interaction -------------------------------------------------------

	// The surface this page is shown on. Everything the interaction needs from
	// the outside world goes through it.
	void setHost(PageHost* h) { pageHost = h; }
	PageHost* host() const { return pageHost; }

	// Input, in world coordinates and free of any toolkit. The shell translates
	// and calls these; both shells call the same ones.
	void OnMouseDown(const input::PointerEvent& event);
	void mouseLeftDown(const input::PointerEvent& event);
	void mouseRightDown(const input::PointerEvent& event);
	void OnMouseUp(const input::PointerEvent& event);
	void OnMouseMove(const input::PointerEvent& event);
	void OnMouseEnter(const input::PointerEvent& event);
	bool OnKeyDown(const input::KeyEvent& event);

	// Abandon whatever drag is in progress (Escape, or a lost pointer capture).
	void cancelDrag();

	// Editing.
	void addGate(std::string gate, GLPoint2f m);
	void deleteSelection();
	void rotateSelection();
	void unselectAllGates();
	void unselectAllWires();
	void copyBlockToClipboard();
	void cutSelectionToClipboard();
	void pasteBlockFromClipboard();

	// Camera moves the page offers as commands.
	void setZoomAll();
	void zoomIn();
	void zoomOut();

	// Refresh the collision index, then ask for a repaint.
	virtual void updatePage();

	// Wire up two things the user dragged between.
	klsCommand* createGateWireConnectionCommand(IDType gateId, const std::string& hotspot, IDType wireId);
	klsCommand* createGateConnectionCommand(IDType gate1Id, const std::string& hotspot1,
	                                        IDType gate2Id, const std::string& hotspot2);

	// Tag a command with this page, then submit it to the undo history, so undo
	// and redo can return to where the edit happened.
	void submitCommand(klsCommand* cmd);

	// Drop every gate and wire, and everything the interaction was holding.
	void clearCircuit();

	// Interactive overlays -- the hovered pin, connection bulbs, the drag box --
	// drawn live on top of the circuit rather than recorded with it.
	void drawOverlaysInto(cl::render::Scene& scene);

	// Called when a gate leaves the page, so anything tracking it can let go.
	virtual void onGateRemoved(unsigned long id);

protected:
	PageHost* pageHost = nullptr;

	// Collision proxies for the pointer and the rubber-band box, so hit testing
	// and drag-select go through the same index as everything else.
	klsCollisionObject* mouse = nullptr;
	klsCollisionObject* snapMouse = nullptr;
	klsCollisionObject* dragselectbox = nullptr;

	std::vector< unsigned long > selectedGates;
	std::vector< unsigned long > selectedWires;

	// Hotspot and wire highlights.
	unsigned long hotspotGate = 0;   // the gate whose hotspot is highlighted
	std::string hotspotHighlight;    // "" when none is
	std::vector< GLPoint2f > potentialConnectionHotspots;
	bool drawWireHover = false;
	unsigned long wireHoverID = 0;
	ConnectionSource currentConnectionSource;

	// When the left button was last pressed, for the click-vs-drag dead zone.
	std::chrono::steady_clock::time_point dragPressTime;
	// When hover work (collision pass + highlight) last ran, to throttle it to
	// ~60Hz on a flood of raw pointer-motion events.
	std::chrono::steady_clock::time_point lastHoverTime;

	bool isWithinPaste = false;   // drag-select stays on until the block drops
	DragState currentDragState = DRAG_NONE;
	cmdPasteBlock* pasteCommand = nullptr;   // held until the block is dropped

	// In DRAG_SELECTION, where everything was before the move, and whether the
	// move is worth recording in the undo stack.
	std::vector< GateState > preMove;
	std::vector< WireState > preMoveWire;
	bool saveMove = false;

	// The gate being placed, in DRAG_NEWGATE, until it is dropped.
	guiGate* newDragGate = nullptr;

	klsCollisionChecker collisionChecker;
	GUICircuit* gCircuit = nullptr;
	CanvasCamera* pageCamera = nullptr;
};

#endif /*CIRCUITPAGE_H_*/
