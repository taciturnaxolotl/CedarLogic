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

#include "gl_defs.h"   // GRID_INTENSITY, MIN_GRID_SCREEN_SPACING
#include "CanvasCamera.h"

namespace cl { namespace render {
	class Scene;
	struct RenderStyle;
	struct Transform;
} }

class guiGate;
class guiWire;

class CircuitPage {
public:
	virtual ~CircuitPage() {}

	std::unordered_map< unsigned long, guiGate* > gateList;
	std::unordered_map< unsigned long, guiWire* > wireList;

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
};

#endif /*CIRCUITPAGE_H_*/
