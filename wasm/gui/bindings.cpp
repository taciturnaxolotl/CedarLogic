// The browser's handle on CedarLogic's core.
//
// This is a shell, not a port: the gates, the wires, the geometry, and the
// drawing all come from src/gui unchanged. What lives here is the boundary --
// creating a page, placing gates on it, recording a frame into a SceneBuffer,
// and handing that buffer to JavaScript as a view over wasm memory.

#include <emscripten/bind.h>

#include <cstdio>
#include <string>
#include <vector>

#include "CanvasCamera.h"
#include "CircuitPage.h"
#include "GUICircuit.h"
#include "GateLibrary.h"
#include "guiGate.h"
#include "guiWire.h"
#include "klsBBox.h"
#include "render/RenderStyle.h"

#include "SceneBuffer.h"

using namespace emscripten;

namespace {

// The gate definitions, parsed once. Embedded into the wasm filesystem at build
// time (see CMakeLists.txt) so the module is self-contained: no fetch, no
// ordering problem between the library arriving and the first gate being made.
const char *kGateDefsPath = "/res/cl_gatedefs.xml";

bool ensureLibraryLoaded(std::string &error) {
	if (!gateLibrary().libraries.empty()) return true;
	// The constructor parses, then destroys its parser -- so calling parseFile()
	// afterwards would read through a dangling pointer.
	gateLibrary().libParser = LibraryParse(kGateDefsPath);
	if (gateLibrary().libraries.empty()) {
		error = "gate library did not parse";
		return false;
	}
	return true;
}

}  // namespace

// One circuit page plus the document it belongs to. The desktop pairs a
// GUICircuit with N GUICanvas pages; here it is a GUICircuit with one
// CircuitPage, which is the same pairing minus the window.
//
// It is the camera's host: the browser owns the viewport size (the shell tells
// us on resize) and the repaint schedule (requestAnimationFrame, which the
// shell drives -- so a repaint request here just raises a flag).
class Document : public CameraHost {
public:
	Document() {
		std::string error;
		ensureLibraryLoaded(error);
		fLoadError = error;
		fCamera.setHost(this);
		// One world unit per grid line, matching klsGLCanvas's constructor.
		fCamera.setGridSpacing(1.0f, 1.0f);
	}

	~Document() {
		for (auto &entry : fPage.gateList) delete entry.second;
		fPage.gateList.clear();
	}

	std::string loadError() const { return fLoadError; }

	// --- CameraHost --------------------------------------------------------

	int cameraViewportWidth() const override { return fViewW; }
	int cameraViewportHeight() const override { return fViewH; }
	void cameraRepaint() override { fDirty = true; }
	// Hover and drag tracking arrive with the interaction port; until then a
	// camera move has nothing else to update.
	void cameraPointerFollowed() override {}

	// --- camera ------------------------------------------------------------

	// The size of the drawing surface in CSS pixels, which is the space the
	// camera measures pan and zoom in.
	void setViewportSize(int w, int h) { fViewW = w; fViewH = h; fDirty = true; }

	double getZoom() const { return fCamera.getZoom(); }
	double panX() const { GLdouble x, y; fCamera.getPan(x, y); return x; }
	double panY() const { GLdouble x, y; fCamera.getPan(x, y); return y; }

	void translatePan(double dx, double dy) { fCamera.translatePan(dx, dy); }

	// Zoom by `notches` wheel steps about a point given in CSS pixels, so the
	// world point under the cursor stays under the cursor -- the same
	// arithmetic, and therefore the same feel, as the desktop wheel.
	void zoomAt(long notches, int px, int py) {
		fCamera.zoomToMouse(notches, fCamera.mapToWorld(px, py));
	}

	// Fit the whole circuit to the view, the way the desktop's spacebar does.
	void zoomAll() {
		klsBBox world;
		for (auto &g : fPage.gateList) if (g.second) world.addBBox(g.second->getBBox());
		for (auto &w : fPage.wireList) if (w.second) world.addBBox(w.second->getBBox());
		if (world.empty()) {
			fCamera.setViewport(GLPoint2f(-50, 50), GLPoint2f(50, -50));
			return;
		}
		// A margin so the outermost gates are not flush against the edge.
		const float mx = (world.getRight() - world.getLeft()) * 0.05f + 1.0f;
		const float my = (world.getTop() - world.getBottom()) * 0.05f + 1.0f;
		fCamera.setViewport(GLPoint2f(world.getLeft() - mx, world.getTop() + my),
		                    GLPoint2f(world.getRight() + mx, world.getBottom() - my));
	}

	// A CSS-pixel point in world coordinates, for hit testing from the shell.
	double worldX(int px, int py) const { return fCamera.mapToWorld(px, py).x; }
	double worldY(int px, int py) const { return fCamera.mapToWorld(px, py).y; }

	// Whether anything has changed since the last render.
	bool isDirty() const { return fDirty; }

	// Every gate type the library knows, so the shell can build a palette
	// without a second copy of the gate list.
	std::vector<std::string> gateTypes() const {
		std::vector<std::string> out;
		for (const auto &lib : gateLibrary().libraries)
			for (const auto &gate : lib.second) out.push_back(gate.first);
		return out;
	}

	// Place a gate of `type` at (x, y) in world coordinates. Returns its id, or
	// -1 if the library has no such gate.
	long addGate(const std::string &type, float x, float y) {
		// createGate falls back to a bare guiGate for a name it does not know,
		// which draws as nothing at all. Refuse here instead, so a typo in the
		// shell surfaces as an error rather than an invisible gate.
		if (!gateLibrary().gateNameToLibrary.count(type)) return -1;
		guiGate *gate = fCircuit.createGate(type, -1, true);
		if (gate == NULL) return -1;
		const long id = fNextId++;
		gate->setGLcoords(x, y);
		fPage.gateList[id] = gate;
		fDirty = true;
		return id;
	}

	// Record one frame at the live camera. The recorded stream is readable until
	// the next call.
	void render(float contentScale) {
		fScene.clear();
		cl::render::RenderStyle style = cl::render::RenderStyle::screen();
		fPage.renderLiveToScene(fScene, style, fCamera, contentScale);
		fDirty = false;
	}

	// The page background the current style asks for, as a CSS colour. The shell
	// clears to this rather than picking its own: the schematic's colours were
	// chosen against it, and a shell that guessed would wash them out.
	std::string background() const {
		const cl::render::Color c = cl::render::RenderStyle::screen().background();
		char buf[64];
		snprintf(buf, sizeof buf, "rgba(%d,%d,%d,%g)", (int)(c.r * 255 + 0.5f),
		         (int)(c.g * 255 + 0.5f), (int)(c.b * 255 + 0.5f), (double)c.a);
		return buf;
	}

	// The recorded frame as an offset into wasm memory plus a length, so
	// JavaScript can wrap it in a Float32Array without copying. The offset is
	// only valid until the next render().
	uintptr_t sceneData() const { return (uintptr_t)fScene.data(); }
	unsigned sceneLength() const { return (unsigned)fScene.size(); }

	// Strings referenced by text commands, in handle order.
	std::vector<std::string> sceneStrings() const { return fScene.strings(); }

private:
	GUICircuit fCircuit;
	CircuitPage fPage;
	CanvasCamera fCamera;
	cl::wasm::SceneBuffer fScene;
	std::string fLoadError;
	long fNextId = 0;
	int fViewW = 0;
	int fViewH = 0;
	bool fDirty = true;
};

EMSCRIPTEN_BINDINGS(cedarlogic_gui) {
	register_vector<std::string>("StringList");

	class_<Document>("Document")
		.constructor<>()
		.function("loadError", &Document::loadError)
		.function("gateTypes", &Document::gateTypes)
		.function("addGate", &Document::addGate)
		.function("background", &Document::background)
		.function("setViewportSize", &Document::setViewportSize)
		.function("getZoom", &Document::getZoom)
		.function("panX", &Document::panX)
		.function("panY", &Document::panY)
		.function("translatePan", &Document::translatePan)
		.function("zoomAt", &Document::zoomAt)
		.function("zoomAll", &Document::zoomAll)
		.function("worldX", &Document::worldX)
		.function("worldY", &Document::worldY)
		.function("isDirty", &Document::isDirty)
		.function("render", &Document::render)
		.function("sceneData", &Document::sceneData)
		.function("sceneLength", &Document::sceneLength)
		.function("sceneStrings", &Document::sceneStrings);
}
