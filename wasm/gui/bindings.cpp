// The browser's handle on CedarLogic's core.
//
// This is a shell, not a port: the gates, the wires, the geometry, and the
// drawing all come from src/gui unchanged. What lives here is the boundary --
// creating a page, placing gates on it, recording a frame into a SceneBuffer,
// and handing that buffer to JavaScript as a view over wasm memory.

#include <emscripten/bind.h>

#include <string>
#include <vector>

#include "CircuitPage.h"
#include "GUICircuit.h"
#include "GateLibrary.h"
#include "guiGate.h"
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
class Document {
public:
	Document() {
		std::string error;
		ensureLibraryLoaded(error);
		fLoadError = error;
	}

	~Document() {
		for (auto &entry : fPage.gateList) delete entry.second;
		fPage.gateList.clear();
	}

	std::string loadError() const { return fLoadError; }

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
		return id;
	}

	// Record one frame, fitting the whole circuit into a deviceW x deviceH
	// image. The recorded stream is readable until the next call.
	void render(int deviceW, int deviceH) {
		fScene.clear();
		cl::render::RenderStyle style = cl::render::RenderStyle::screen();
		fPage.renderToScene(fScene, style, deviceW, deviceH,
		                    kGridSpacing, kGridSpacing);
	}

	// The recorded frame as an offset into wasm memory plus a length, so
	// JavaScript can wrap it in a Float32Array without copying. The offset is
	// only valid until the next render().
	uintptr_t sceneData() const { return (uintptr_t)fScene.data(); }
	unsigned sceneLength() const { return (unsigned)fScene.size(); }

	// Strings referenced by text commands, in handle order.
	std::vector<std::string> sceneStrings() const { return fScene.strings(); }

private:
	// The desktop reads grid spacing off klsGLCanvas, which defaults to one
	// world unit in each direction. Match it until the shell exposes a setting.
	static const int kGridSpacing = 1;

	GUICircuit fCircuit;
	CircuitPage fPage;
	cl::wasm::SceneBuffer fScene;
	std::string fLoadError;
	long fNextId = 0;
};

EMSCRIPTEN_BINDINGS(cedarlogic_gui) {
	register_vector<std::string>("StringList");

	class_<Document>("Document")
		.constructor<>()
		.function("loadError", &Document::loadError)
		.function("gateTypes", &Document::gateTypes)
		.function("addGate", &Document::addGate)
		.function("render", &Document::render)
		.function("sceneData", &Document::sceneData)
		.function("sceneLength", &Document::sceneLength)
		.function("sceneStrings", &Document::sceneStrings);
}
