// The browser's handle on CedarLogic's core.
//
// This is a shell, not a port: the gates, the wires, the geometry, and the
// drawing all come from src/gui unchanged. What lives here is the boundary --
// creating a page, placing gates on it, recording a frame into a SceneBuffer,
// and handing that buffer to JavaScript as a view over wasm memory.

#include <emscripten/bind.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "CanvasCamera.h"
#include "CircuitObserver.h"
#include "EmbeddedRes.h"
#include "CircuitParse.h"
#include "SimBridge.h"
#include "Settings.h"
#include "threadLogic.h"
#include "wx/clipbrd.h"
#include "CircuitPage.h"
#include "GUICircuit.h"
#include "GateLibrary.h"
#include "guiGate.h"
#include "guiWire.h"
#include "klsBBox.h"
#include "render/RenderStyle.h"
#include "render/Thumbnail.h"
#include "commands.h"

#include <cfloat>

#include "SceneBuffer.h"

using namespace emscripten;

namespace {

// The gate definitions, parsed once from the copy compiled into the module
// (see EmbeddedRes.h). Nothing is fetched and no filesystem is involved, so
// there is no ordering problem between the library arriving and the first gate
// being made.
bool ensureLibraryLoaded(std::string &error) {
	if (!gateLibrary().libraries.empty()) return true;

	const std::string xml = cl::res::text("cl_gatedefs.xml");
	if (xml.empty()) {
		error = "the gate library is missing from this build";
		return false;
	}

	// The constructor parses, then destroys its parser -- so calling parseFile()
	// afterwards would read through a dangling pointer.
	gateLibrary().libParser = LibraryParse(xml);
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
// One page and the camera looking at it. The desktop gives every canvas its own
// pan and zoom, so switching tabs returns you to where you were rather than
// wherever the last page was left.
struct WebPage {
	CircuitPage page;
	CanvasCamera camera;
};

class Document : public CameraHost, public PageHost, public CircuitObserver {
public:
	Document() {
		std::string error;
		ensureLibraryLoaded(error);
		fLoadError = error;
		fCircuit.setObserver(this);
		addPage();

		// Stand the logic core up without a thread. The document owns it and
		// pumps it from stepSimulation.
		fCircuit.setObserver(this);

		fLogic = new threadLogic();
		fLogic->initCore();
		simBridge().logicThread = fLogic;
		// One world unit per grid line, matching klsGLCanvas's constructor.
		cam().setGridSpacing(1.0f, 1.0f);
	}

	~Document() {
		clearCircuit();
		simBridge().logicThread = nullptr;
		delete fLogic;
	}

	std::string loadError() const { return fLoadError; }

	// --- CameraHost --------------------------------------------------------

	int cameraViewportWidth() const override { return fViewW; }
	int cameraViewportHeight() const override { return fViewH; }
	void cameraRepaint() override { fDirty = true; }
	// Hover and drag tracking arrive with the interaction port; until then a
	// camera move has nothing else to update.
	void cameraPointerFollowed() override {}

	// --- pages -------------------------------------------------------------

	// The current page, and the camera looking at it. Everything the shell asks
	// of "the circuit" means the page in front of you, as it does on the
	// desktop where the question is answered by which tab is selected.
	CircuitPage& page() { return fPages[fCurrent]->page; }
	const CircuitPage& page() const { return fPages[fCurrent]->page; }
	CanvasCamera& cam() { return fPages[fCurrent]->camera; }
	const CanvasCamera& cam() const { return fPages[fCurrent]->camera; }

	int pageCount() const { return (int)fPages.size(); }
	int currentPage() const { return (int)fCurrent; }

	void setCurrentPage(int index) {
		if (index < 0 || index >= (int)fPages.size()) return;
		fCurrent = (size_t)index;
		fDirty = true;
	}

	// Add a page and switch to it, as File > New Tab does.
	int addPage() {
		fPages.push_back(std::unique_ptr<WebPage>(new WebPage()));
		WebPage& added = *fPages.back();
		added.page.setCircuit(&fCircuit);
		added.page.setCamera(&added.camera);
		added.page.setHost(this);
		added.camera.setHost(this);
		// One world unit per grid line, matching klsGLCanvas's constructor.
		added.camera.setGridSpacing(1.0f, 1.0f);
		fCurrent = fPages.size() - 1;
		fDirty = true;
		return (int)fCurrent;
	}

	// Close a page. The last one stays: a document with no page has nothing to
	// draw and nowhere to put a gate.
	bool removePage(int index) {
		if (fPages.size() <= 1) return false;
		if (index < 0 || index >= (int)fPages.size()) return false;
		fPages[index]->page.clearCircuit();
		fPages.erase(fPages.begin() + index);
		if (fCurrent >= fPages.size()) fCurrent = fPages.size() - 1;
		fDirty = true;
		return true;
	}

	int gatesOnPage(int index) const {
		if (index < 0 || index >= (int)fPages.size()) return 0;
		return (int)fPages[index]->page.gateList.size();
	}

	// --- CircuitObserver ---------------------------------------------------

	// The circuit asks for a repaint after every step, whether or not anything
	// it drew actually changed -- so this is recorded separately from an edit or
	// a camera move. isDirty() settles it by comparing the content signature,
	// which is what stops an idle circuit from re-recording and re-drawing sixty
	// times a second for nothing.
	void circuitRedrawNeeded() override { fSimDirty = true; }

	// No oscilloscope in the shell yet; a running circuit still generates
	// samples, and this is where they will land.
	void oscopeDataAdded() override {}
	void oscopeSignalsChanged() override {}

	// --- PageHost ----------------------------------------------------------

	// The browser reports the pointer in CSS pixels; the page thinks in world
	// coordinates, so the mapping happens here and nowhere else.
	GLPoint2f pointerPos() const override { return fPointer; }
	GLPoint2f dragStart(input::Button b) const override { return fDragStart[(int)b]; }
	GLPoint2f dragEnd(input::Button b) const override { return fDragEnd[(int)b]; }
	bool isDragging(input::Button b) const override { return fDragging[(int)b]; }

	void beginDrag(input::Button b) override {
		fDragging[(int)b] = true;
		fDragStart[(int)b] = fPointer;
		fDragEnd[(int)b] = fPointer;
	}

	void endDrag(input::Button b) override {
		fDragging[(int)b] = false;
		fDragEnd[(int)b] = fPointer;
		fDirty = true;
	}

	void requestRepaint() override { fDirty = true; }

	// The shell owns the picker; raise a flag it can poll on the next frame.
	void requestQuickAdd() override { fQuickAddWanted = true; }

	bool takeQuickAddRequest() {
		const bool wanted = fQuickAddWanted;
		fQuickAddWanted = false;
		return wanted;
	}

	// --- input -------------------------------------------------------------

	// Pointer position in CSS pixels; everything below works from it.
	void pointerAt(int px, int py) { fPointer = cam().mapToWorld(px, py); }

	void pointerDown(int px, int py, int button, bool shift, bool ctrl, bool alt, bool meta) {
		pointerAt(px, py);
		input::PointerEvent e = makeEvent(button, shift, ctrl, alt, meta);
		e.leftIsDown = (button == 0);
		beginDrag(e.button);
        page().OnMouseDown(e);
	}

	void pointerMove(int px, int py, bool leftDown, bool shift, bool ctrl, bool alt, bool meta) {
		pointerAt(px, py);
		input::PointerEvent e = makeEvent(-1, shift, ctrl, alt, meta);
		e.leftIsDown = leftDown;
		page().OnMouseMove(e);
	}

	void pointerUp(int px, int py, int button, bool doubleClick, bool shift, bool ctrl, bool alt, bool meta) {
		pointerAt(px, py);
		input::PointerEvent e = makeEvent(button, shift, ctrl, alt, meta);
		e.doubleClick = doubleClick;
		endDrag(e.button);
		page().OnMouseUp(e);
		// Clicking a toggle or a keypad edits the circuit; let the core see it
		// now rather than on the next step, so the LED lights when the button
		// goes down.
		pumpLogic();
	}

	// `key` is a name from the DOM ("Delete", "ArrowLeft", "a"); anything of
	// length one travels as a character.
	void keyDown(const std::string &key, bool shift, bool ctrl, bool alt, bool meta) {
		cedar_shim::shiftHeld() = shift;
		input::KeyEvent e;
		e.mods.shift = shift;
		e.mods.ctrl = ctrl;
		e.mods.alt = alt;
		e.mods.cmd = meta;

		if (key == "Delete")          e.key = input::Key::Delete;
		else if (key == "Backspace")  e.key = input::Key::Backspace;
		else if (key == "Escape")     e.key = input::Key::Escape;
		else if (key == " ")          e.key = input::Key::Space;
		else if (key == "ArrowLeft")  e.key = input::Key::Left;
		else if (key == "ArrowRight") e.key = input::Key::Right;
		else if (key == "ArrowUp")    e.key = input::Key::Up;
		else if (key == "ArrowDown")  e.key = input::Key::Down;
		else if (key == "+")          e.key = input::Key::Plus;
		else if (key == "-")          e.key = input::Key::Minus;
		else if (key == "=")          e.key = input::Key::Equals;
		else if (key.size() == 1)   { e.key = input::Key::Character; e.ch = (char32_t)key[0]; }
		else return;

		page().OnKeyDown(e);
	}

	// The page's clipboard text. The browser's own clipboard is asynchronous and
	// needs a user gesture, so the shell syncs it around copy and paste rather
	// than the page reaching for it mid-edit.
	std::string clipboardText() const { return wxTheClipboard->text(); }
	void setClipboardText(const std::string &text) { wxTheClipboard->setText(wxString(text)); }

	// --- editing commands --------------------------------------------------

	void deleteSelection() { page().deleteSelection(); }
	void rotateSelection() { page().rotateSelection(); }
	void copySelection() { page().copyBlockToClipboard(); }
	void cutSelection() { page().cutSelectionToClipboard(); }
	void paste() { page().pasteBlockFromClipboard(); }

	bool undo() {
		const bool ok = fCircuit.GetCommandProcessor()->Undo();
		if (ok) { page().updatePage(); }
		return ok;
	}

	bool redo() {
		const bool ok = fCircuit.GetCommandProcessor()->Redo();
		if (ok) { page().updatePage(); }
		return ok;
	}

	bool canUndo() const { return fCircuit.GetCommandProcessor()->CanUndo(); }
	bool canRedo() const { return fCircuit.GetCommandProcessor()->CanRedo(); }

	// Interaction state, for diagnosing the shell against the desktop.
	std::string debugState() const {
		Document *self = const_cast<Document *>(this);
		char buf[256];
		snprintf(buf, sizeof buf,
		         "page=%d/%d drag=%d sel=%d hotspot='%s' ptr=%.2f,%.2f dragging=%d",
		         (int)fCurrent, (int)fPages.size(),
		         (int)self->page().dragState(), self->page().selectedCount(),
		         self->page().hoveredHotspot().c_str(), fPointer.x, fPointer.y,
		         fDragging[(int)input::Button::Left] ? 1 : 0);
		return buf;
	}

	int selectedCount() const { return const_cast<Document *>(this)->page().selectedCount(); }

	// --- camera ------------------------------------------------------------

	// The size of the drawing surface in CSS pixels, which is the space the
	// camera measures pan and zoom in.
	void setViewportSize(int w, int h) { fViewW = w; fViewH = h; fDirty = true; }

	double getZoom() const { return cam().getZoom(); }
	double panX() const { GLdouble x, y; cam().getPan(x, y); return x; }
	double panY() const { GLdouble x, y; cam().getPan(x, y); return y; }

	void translatePan(double dx, double dy) { cam().translatePan(dx, dy); }

	// Zoom by `notches` wheel steps about a point given in CSS pixels, so the
	// world point under the cursor stays under the cursor -- the same
	// arithmetic, and therefore the same feel, as the desktop wheel.
	void zoomAt(long notches, int px, int py) {
		cam().zoomToMouse(notches, cam().mapToWorld(px, py));
	}

	// Zoom by a fraction of a step. One step is a wheel notch; a trackpad passes
	// the fraction its travel is worth, so the view glides instead of jumping.
	void zoomAtBy(double steps, int px, int py) {
		cam().zoomToPoint(steps, cam().mapToWorld(px, py));
	}

	// Pan by scroll steps, the way the desktop's wheel handler does: one step is
	// PAN_STEP pixels' worth of world at the current zoom. The constant stays
	// here rather than in the shell so the two cannot drift.
	//
	// Steps are fractional, because a trackpad's are: rounding them to whole
	// lines would truncate an ordinary two-finger drag to nothing at all.
	void scrollPan(double stepsX, double stepsY) {
		const GLdouble amount = PAN_STEP * cam().getZoom();
		cam().translatePan(amount * stepsX, amount * stepsY);
	}

	// Fit the whole circuit to the view, the way the desktop's spacebar does.
	void zoomAll() {
		klsBBox world;
		for (auto &g : page().gateList) if (g.second) world.addBBox(g.second->getBBox());
		for (auto &w : page().wireList) if (w.second) world.addBBox(w.second->getBBox());
		if (world.empty()) {
			cam().setViewport(GLPoint2f(-50, 50), GLPoint2f(50, -50));
			return;
		}
		// A margin so the outermost gates are not flush against the edge.
		const float mx = (world.getRight() - world.getLeft()) * 0.05f + 1.0f;
		const float my = (world.getTop() - world.getBottom()) * 0.05f + 1.0f;
		cam().setViewport(GLPoint2f(world.getLeft() - mx, world.getTop() + my),
		                    GLPoint2f(world.getRight() + mx, world.getBottom() - my));
	}

	// A CSS-pixel point in world coordinates, for hit testing from the shell.
	double worldX(int px, int py) const { return cam().mapToWorld(px, py).x; }
	double worldY(int px, int py) const { return cam().mapToWorld(px, py).y; }

	// Whether the next frame would look different from the last one.
	//
	// An edit, a camera move, or anything touching the interactive overlays
	// answers yes outright: the overlays are drawn live and are not part of the
	// content signature. A simulation step only counts if it actually changed
	// something a viewer could see, which page().renderContentKey() answers by folding
	// every gate's and wire's appearance into one number -- the same signature
	// the desktop keys its retained picture on.
	bool isDirty() {
		if (fDirty) return true;
		if (!fSimDirty) return false;

		if (page().renderContentKey() == fLastContentKey) {
			fSimDirty = false;
			return false;
		}
		return true;
	}

	// Every gate type the library knows, so the shell can build a palette
	// without a second copy of the gate list.
	std::vector<std::string> gateTypes() const {
		std::vector<std::string> out;
		for (const auto &lib : gateLibrary().libraries)
			for (const auto &gate : lib.second) out.push_back(gate.first);
		return out;
	}

	// --- simulation --------------------------------------------------------

	// The desktop runs the logic core on its own thread and pumps it from two
	// timers. A browser has one thread, so the same three steps happen inline
	// here, once per frame: queue a step, let the core work through the queue,
	// then apply what it sent back.
	//
	// `elapsedMs` is real time since the last call. Steps are taken in whole
	// multiples of the configured time step, and a long gap (a backgrounded tab)
	// is capped rather than made up all at once -- simulating minutes of circuit
	// takes longer than the minutes took to pass.
	void stepSimulation(double elapsedMs) {
		if (!fSimulate || fCircuit.panic) return;

		const long step = appConfig().timeStepMod > 0 ? appConfig().timeStepMod : 25;
		fSimAccumulator += elapsedMs;

		const double maxCatchUp = (double)step * kMaxCatchUpSteps;
		if (fSimAccumulator > maxCatchUp) fSimAccumulator = maxCatchUp;

		const long steps = (long)(fSimAccumulator / step);
		if (steps <= 0) return;
		fSimAccumulator -= (double)steps * step;

		fCircuit.lastTime = (int)(steps * step);
		fCircuit.lastTimeMod = (int)step;
		fCircuit.lastNumSteps = (int)steps;
		fCircuit.sendMessageToCore(klsMessage::Message(
			klsMessage::MT_STEPSIM, new klsMessage::Message_STEPSIM(steps)));
		fCircuit.setSimulate(false);

		pumpLogic();
	}

	// Run the core over everything queued for it, then apply its replies. Also
	// called after a direct edit (flipping a toggle) so the change lands without
	// waiting for the next step.
	void pumpLogic() {
		if (fLogic == nullptr) return;
		fLogic->checkMessages();

		std::deque<klsMessage::Message> batch;
		batch.swap(simBridge().dLOGICtoGUI);
		while (!batch.empty()) {
			fCircuit.parseMessage(batch.front());
			batch.pop_front();
		}
	}

	bool isSimulating() const { return fSimulate; }
	void setSimulating(bool on) {
		fSimulate = on;
		// Starting again should not try to make up the time spent paused.
		fSimAccumulator = 0.0;
		if (on) fCircuit.panic = false;
	}

	// One step's worth of circuit, regardless of wall time -- the desktop's
	// step button.
	void stepOnce() {
		const long step = appConfig().timeStepMod > 0 ? appConfig().timeStepMod : 25;
		fCircuit.lastTime = (int)step;
		fCircuit.lastTimeMod = (int)step;
		fCircuit.lastNumSteps = 1;
		fCircuit.sendMessageToCore(klsMessage::Message(
			klsMessage::MT_STEPSIM, new klsMessage::Message_STEPSIM(1)));
		fCircuit.setSimulate(false);
		pumpLogic();
		fDirty = true;
	}

	// The core stopped because the circuit could not keep up.
	bool inPanic() const { return fCircuit.panic; }
	void clearPanic() { fCircuit.panic = false; }

	// --- view and edit state -----------------------------------------------

	// The desktop's View menu toggles, which change what the renderer draws
	// rather than what the circuit is.
	bool gridlinesVisible() const { return appConfig().appSettings.gridlineVisible; }
	void setGridlinesVisible(bool on) {
		appConfig().appSettings.gridlineVisible = on;
		fDirty = true;
	}

	bool wireConnectionsVisible() const { return appConfig().appSettings.wireConnVisible; }
	void setWireConnectionsVisible(bool on) {
		appConfig().appSettings.wireConnVisible = on;
		fDirty = true;
	}

	// Milliseconds of circuit time per simulation step -- the desktop's toolbar
	// slider. Smaller is a finer-grained simulation and more work per second.
	int timeStep() const { return (int)appConfig().timeStepMod; }
	void setTimeStep(int ms) {
		appConfig().timeStepMod = (unsigned long)(ms < 1 ? 1 : ms);
	}

	// The desktop's lock button: editing off, simulation still running.
	bool isLocked() const override { return fLocked; }
	void setLocked(bool on) { fLocked = on; }

	// --- gate parameters ---------------------------------------------------

	// A double-click asks for a parameter editor. The page names the gate; the
	// shell polls for it and opens its panel.
	void requestGateParams(unsigned long gateId) override { fParamGate = (long)gateId; }

	long takeParamRequest() {
		const long id = fParamGate;
		fParamGate = -1;
		return id;
	}

	// What the library says this gate's editable parameters are, and what they
	// currently hold. The same list the desktop's dialog builds itself from, so
	// the two editors offer the same fields in the same order.
	emscripten::val gateParams(long gateId) {
		emscripten::val out = emscripten::val::array();
		auto found = page().gateList.find((unsigned long)gateId);
		if (found == page().gateList.end() || found->second == NULL) return out;
		guiGate *gate = found->second;

		LibraryGate def;
		if (!gateLibrary().libParser.getGate(gate->getLibraryGateName(), def)) return out;

		for (const lgDlgParam &p : def.dlgParams) {
			emscripten::val entry = emscripten::val::object();
			entry.set("label", p.textLabel);
			entry.set("name", p.name);
			entry.set("gui", p.isGui);
			entry.set("type", p.type);
			// FLT_MAX either side means "no range given"; JavaScript would rather
			// hear nothing than a number that big.
			if (p.Rmin > -FLT_MAX) entry.set("min", p.Rmin);
			if (p.Rmax < FLT_MAX) entry.set("max", p.Rmax);
			entry.set("value", p.isGui ? gate->getGUIParam(p.name)
			                           : gate->getLogicParam(p.name));
			out.call<void>("push", entry);
		}
		return out;
	}

	// Editing a gate is one command however many fields changed, so undo puts
	// all of them back together -- the same shape as the desktop's dialog,
	// which submits one cmdSetParams when you press OK.
	void beginParamEdit() {
		fEditGui.clear();
		fEditLogic.clear();
	}

	void setParam(const std::string &name, bool isGui, const std::string &value) {
		(isGui ? fEditGui : fEditLogic)[name] = value;
	}

	bool commitParamEdit(long gateId) {
		auto found = page().gateList.find((unsigned long)gateId);
		if (found == page().gateList.end() || found->second == NULL) return false;
		if (fEditGui.empty() && fEditLogic.empty()) return false;

		page().submitCommand(new cmdSetParams(&fCircuit, (unsigned long)gateId,
		                               paramSet(&fEditGui, &fEditLogic)));
		pumpLogic();
		page().updatePage();
		fDirty = true;
		return true;
	}

	// --- palette -----------------------------------------------------------

	// The libraries, in the order the gate-definition file lists them, which is
	// the order the desktop's section chooser shows.
	std::vector<std::string> libraryNames() const {
		std::vector<std::string> out;
		for (const auto &lib : gateLibrary().libraries) out.push_back(lib.first);
		return out;
	}

	std::vector<std::string> gatesInLibrary(const std::string &library) const {
		std::vector<std::string> out;
		auto lib = gateLibrary().libraries.find(library);
		if (lib == gateLibrary().libraries.end()) return out;
		for (const auto &gate : lib->second) out.push_back(gate.first);
		return out;
	}

	// The library a gate belongs to, for grouping a search result.
	std::string libraryOf(const std::string &type) const {
		auto it = gateLibrary().gateNameToLibrary.find(type);
		return it == gateLibrary().gateNameToLibrary.end() ? std::string() : it->second;
	}

	// Record one gate, framed in a square tile, into the scene buffer -- the
	// same drawToScene the canvas uses and the same framing the desktop's
	// palette uses, so a tile and the placed gate cannot look different.
	//
	// The gate is built, drawn and dropped: the palette holds one tile per
	// library entry, and keeping a live guiGate in each -- for something only
	// ever used to draw a picture -- is a lot of circuit to carry around.
	bool renderGateThumbnail(const std::string &type, int sizePx) {
		if (sizePx <= 0) return false;
		guiGate *gate = fCircuit.createGate(type, -1, true);
		if (gate == NULL) return false;
		gate->setGLcoords(0, 0);
		gate->calcBBox();

		fScene.clear();
		const cl::render::Transform t =
			cl::render::thumbnailTransform(gate->getModelDrawBBox(), sizePx);
		const cl::render::RenderStyle style = cl::render::RenderStyle::print();
		fScene.setViewport(t);
		gate->drawToScene(fScene, style);

		// The gate was never put on the page, so nothing else owns it.
		fCircuit.deleteGate(gate->getID());
		return true;
	}

	// Record the whole circuit fitted to a width x height image -- the bbox-fit
	// path the desktop's "Export as Image" uses, not the live camera. The shell
	// replays it into an offscreen canvas and hands back a PNG.
	void renderForExport(int width, int height, bool withGrid) {
		fScene.clear();
		cl::render::RenderStyle style = cl::render::RenderStyle::print();
		style.showGrid = withGrid;
		page().renderToScene(fScene, style, width, height,
		              cam().horizSpacing(), cam().vertSpacing());
		// The export reuses the buffer the frame loop draws from, so the next
		// frame has to record again rather than replay this.
		fDirty = true;
	}

	// --- files -------------------------------------------------------------

	// Read a .cdl. Legacy v1/v2 files are migrated on the way in, exactly as on
	// the desktop -- it is the same reader. Returns "" on success, or a message.
	std::string loadCircuit(const std::string &text) {
		cl::LoadResult loaded;
		try {
			loaded = cl::loadCircuit(text);
		} catch (const std::exception &e) {
			return std::string("could not read the circuit: ") + e.what();
		}

		clearCircuit();

		// The file names the page each gate belongs to and may name one we have
		// not got; grow to answer, exactly as MainFrame does. Merging every page
		// onto one -- which is what a single-page provider does -- silently
		// destroys the structure, and saving then writes the merged result.
		CircuitParse parser([this](int index) -> CircuitPage* {
			while (index > (int)fPages.size() - 1) addPage();
			return &fPages[index]->page;
		});
		parser.applyLoaded(loaded);
		setCurrentPage(0);

		// What the migration and the apply had to say about the file.
		fNotices.clear();
		for (const cl::MigrationNotice &n : loaded.notices) fNotices.push_back(n.summary);
		for (const cl::MigrationNotice &n : parser.getApplyNotices()) fNotices.push_back(n.summary);

		// Build the collision index for what just arrived; hit testing and
		// drag-select both read it, so without this a freshly loaded circuit
		// cannot be clicked on.
		page().updatePage();

		fDirty = true;
		return "";
	}

	// The circuit as v3 .cdl text, for the shell to download.
	std::string saveCircuit() {
		std::vector<CircuitPage *> pages;
		for (auto& p : fPages) pages.push_back(&p->page);
		return CircuitParse::serializeV3(pages);
	}

	// Anything the last load wanted to say: a gate type that no longer exists, a
	// wire that could not be attached. Silence here means a clean read.
	std::vector<std::string> loadNotices() const { return fNotices; }

	void clearCircuit() {
		// The document owns the gates and wires, not the pages: GUICircuit
		// created them and its reset is what frees them and reinitializes the
		// logic core. Deleting them here as well left the document holding
		// dangling pointers that the next load walked straight into.
		for (auto& p : fPages) p->page.clearPage();
		fCircuit.reInitializeLogicCircuit();
		fPages.erase(fPages.begin() + 1, fPages.end());
		fCurrent = 0;
		fDirty = true;
	}

	int gateCount() const { return (int)page().gateList.size(); }
	int wireCount() const {
		int n = 0;
		for (const auto &entry : page().wireList) if (entry.second) n++;
		return n;
	}

	// --- editing -----------------------------------------------------------

	// Place a gate of `type` at (x, y) in world coordinates. Returns its id, or
	// -1 if the library has no such gate.
	long addGate(const std::string &type, float x, float y) {
		// createGate falls back to a bare guiGate for a name it does not know,
		// which draws as nothing at all. Refuse here instead, so a typo in the
		// shell surfaces as an error rather than an invisible gate.
		if (!gateLibrary().gateNameToLibrary.count(type)) return -1;
		guiGate *gate = fCircuit.createGate(type, -1, true);
		if (gate == NULL) return -1;
		// The document assigns the id, and the page must key by the same one --
		// keying by a counter of our own let the two maps disagree, and a
		// command looking the gate up in the document would not find it.
		const long id = (long)gate->getID();
		gate->setGLcoords(x, y);
		page().gateList[id] = gate;
		page().getCollisionChecker().addObject(gate);
		page().updatePage();
		fDirty = true;
		return id;
	}

	// Record one frame at the live camera. The recorded stream is readable until
	// the next call.
	void render(float contentScale) {
		fScene.clear();
		cl::render::RenderStyle style = cl::render::RenderStyle::screen();
		// View > Display Gridlines, the same consultation renderSkiaLive makes.
		style.showGrid = appConfig().appSettings.gridlineVisible;
		page().renderLiveToScene(fScene, style, cam(), contentScale);
		page().drawOverlaysInto(fScene);
		fLastContentKey = page().renderContentKey();
		fDirty = false;
		fSimDirty = false;
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
	cl::wasm::SceneBuffer fScene;
	std::string fLoadError;
	std::vector<std::string> fNotices;
	int fViewW = 0;
	int fViewH = 0;
	bool fDirty = true;
	bool fLocked = false;
	long fParamGate = -1;
	ParameterMap fEditGui;
	ParameterMap fEditLogic;
	// A simulation step asked for a repaint; whether it earns one depends on
	// whether the content signature moved.
	bool fSimDirty = false;
	unsigned long long fLastContentKey = 0;

	std::vector<std::unique_ptr<WebPage>> fPages;
	size_t fCurrent = 0;

	// The logic core, run inline rather than on a thread.
	threadLogic* fLogic = nullptr;
	bool fSimulate = true;
	double fSimAccumulator = 0.0;
	// Cap a catch-up at this many steps, matching the desktop.
	static const int kMaxCatchUpSteps = 40;

	// Pointer and per-button drag tracking, which on the desktop lives on the
	// wx canvas. Three buttons, matching input::Button minus None.
	GLPoint2f fPointer;
	GLPoint2f fDragStart[4];
	GLPoint2f fDragEnd[4];
	bool fDragging[4] = { false, false, false, false };
	bool fQuickAddWanted = false;

	// `pos` is the world point under the pointer. The handlers read it directly
	// as well as through the host, so leaving it at the origin made every drag
	// think the cursor was parked at (0, 0).
	input::PointerEvent makeEvent(int button, bool shift, bool ctrl, bool alt, bool meta) const {
		cedar_shim::shiftHeld() = shift;
		input::PointerEvent e;
		e.pos = fPointer;
		switch (button) {
		case 0:  e.button = input::Button::Left; break;
		case 1:  e.button = input::Button::Middle; break;
		case 2:  e.button = input::Button::Right; break;
		default: e.button = input::Button::None; break;
		}
		e.mods.shift = shift;
		e.mods.ctrl = ctrl;
		e.mods.alt = alt;
		e.mods.cmd = meta;
		return e;
	}
};

EMSCRIPTEN_BINDINGS(cedarlogic_gui) {
	register_vector<std::string>("StringList");

	class_<Document>("Document")
		.constructor<>()
		.function("loadError", &Document::loadError)
		.function("gateTypes", &Document::gateTypes)
		.function("gridlinesVisible", &Document::gridlinesVisible)
		.function("setGridlinesVisible", &Document::setGridlinesVisible)
		.function("wireConnectionsVisible", &Document::wireConnectionsVisible)
		.function("setWireConnectionsVisible", &Document::setWireConnectionsVisible)
		.function("timeStep", &Document::timeStep)
		.function("setTimeStep", &Document::setTimeStep)
		.function("isLocked", &Document::isLocked)
		.function("setLocked", &Document::setLocked)
		.function("takeParamRequest", &Document::takeParamRequest)
		.function("gateParams", &Document::gateParams)
		.function("beginParamEdit", &Document::beginParamEdit)
		.function("setParam", &Document::setParam)
		.function("commitParamEdit", &Document::commitParamEdit)
		.function("libraryNames", &Document::libraryNames)
		.function("gatesInLibrary", &Document::gatesInLibrary)
		.function("libraryOf", &Document::libraryOf)
		.function("renderGateThumbnail", &Document::renderGateThumbnail)
		.function("loadCircuit", &Document::loadCircuit)
		.function("saveCircuit", &Document::saveCircuit)
		.function("renderForExport", &Document::renderForExport)
		.function("loadNotices", &Document::loadNotices)
		.function("clearCircuit", &Document::clearCircuit)
		.function("pageCount", &Document::pageCount)
		.function("currentPage", &Document::currentPage)
		.function("setCurrentPage", &Document::setCurrentPage)
		.function("addPage", &Document::addPage)
		.function("removePage", &Document::removePage)
		.function("gatesOnPage", &Document::gatesOnPage)
		.function("gateCount", &Document::gateCount)
		.function("wireCount", &Document::wireCount)
		.function("addGate", &Document::addGate)
		.function("background", &Document::background)
		.function("pointerDown", &Document::pointerDown)
		.function("pointerMove", &Document::pointerMove)
		.function("pointerUp", &Document::pointerUp)
		.function("keyDown", &Document::keyDown)
		.function("deleteSelection", &Document::deleteSelection)
		.function("rotateSelection", &Document::rotateSelection)
		.function("copySelection", &Document::copySelection)
		.function("cutSelection", &Document::cutSelection)
		.function("paste", &Document::paste)
		.function("clipboardText", &Document::clipboardText)
		.function("setClipboardText", &Document::setClipboardText)
		.function("undo", &Document::undo)
		.function("redo", &Document::redo)
		.function("canUndo", &Document::canUndo)
		.function("canRedo", &Document::canRedo)
		.function("selectedCount", &Document::selectedCount)
		.function("debugState", &Document::debugState)
		.function("takeQuickAddRequest", &Document::takeQuickAddRequest)
		.function("setViewportSize", &Document::setViewportSize)
		.function("getZoom", &Document::getZoom)
		.function("panX", &Document::panX)
		.function("panY", &Document::panY)
		.function("translatePan", &Document::translatePan)
		.function("zoomAt", &Document::zoomAt)
		.function("zoomAtBy", &Document::zoomAtBy)
		.function("scrollPan", &Document::scrollPan)
		.function("zoomAll", &Document::zoomAll)
		.function("worldX", &Document::worldX)
		.function("worldY", &Document::worldY)
		.function("isDirty", &Document::isDirty)
		.function("stepSimulation", &Document::stepSimulation)
		.function("stepOnce", &Document::stepOnce)
		.function("isSimulating", &Document::isSimulating)
		.function("setSimulating", &Document::setSimulating)
		.function("inPanic", &Document::inPanic)
		.function("clearPanic", &Document::clearPanic)
		.function("render", &Document::render)
		.function("sceneData", &Document::sceneData)
		.function("sceneLength", &Document::sceneLength)
		.function("sceneStrings", &Document::sceneStrings);
}
