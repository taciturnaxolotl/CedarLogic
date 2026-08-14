/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   CircuitParse: uses XMLParser to load and save user circuit files.
*****************************************************************************/

#include "CircuitParse.h"
#include "PaletteDrag.h"
#include "RenderMode.h"
#include "GateLibrary.h"
#include "OscopeFrame.h"
#include "MainApp.h"
#include <fstream>
#include <wx/file.h>
#include <sstream>
#include <cerrno>
#include <cstring>

// Windows doesn't define EDQUOT (disk quota exceeded) - define it for compatibility
#ifdef _MSC_VER
#ifndef EDQUOT
#define EDQUOT 122  // POSIX standard value for disk quota exceeded
#endif
#endif

#include "XMLParser.h"
#include "guiGate.h"
#include "guiWire.h"
#include "GUICircuit.h"
#include "GUICanvas.h"
#include <map>
#include <set>
#include <unordered_map>
#include <cstdlib>
#include "../version.h"
#include "migrate.hpp"          // cl::loadCircuit: format detection + migration notices
#include "circuit_file_io.hpp"  // cl::writeCircuitFile: the v3 serializer

DECLARE_APP(MainApp)

CircuitParse::CircuitParse(GUICanvas* glc) {
	// this constructor did not initialiize all its data members, I corrected that
	// note:  gCanvases and fileName are initialized by base class default constructors   KAS
	mParse = nullptr;
	gCanvas = glc;
}

CircuitParse::CircuitParse(string fileName, vector< GUICanvas* > glc) {
	gCanvases = glc;
	gCanvas = glc[0];

	fstream x(fileName.c_str(), ios::in);
	mParse = new XMLParser(&x, false);
	this->fileName = fileName;
}

CircuitParse::~CircuitParse() {
	delete mParse;
}

void CircuitParse::loadFile(string fileName) {
	fstream x(fileName.c_str(), ios::in);
	mParse = new XMLParser(&x, false);
	this->fileName = fileName;
}

// Helper function to check if file has a breaking (major) version change
// Returns true if fileVersion has a newer major version than currentVersion
static bool hasBreakingVersion(const string& fileVersion, const string& currentVersion) {
	// Extract just the version numbers (before the " | " timestamp separator)
	size_t fileSep = fileVersion.find(" | ");
	size_t currSep = currentVersion.find(" | ");

	string fileVer = (fileSep != string::npos) ? fileVersion.substr(0, fileSep) : fileVersion;
	string currVer = (currSep != string::npos) ? currentVersion.substr(0, currSep) : currentVersion;

	// Parse version components (major.minor.patch)
	istringstream fileStream(fileVer);
	istringstream currStream(currVer);

	int fileMajor = 0, fileMinor = 0, filePatch = 0;
	int currMajor = 0, currMinor = 0, currPatch = 0;
	char dot;

	fileStream >> fileMajor >> dot >> fileMinor >> dot >> filePatch;
	currStream >> currMajor >> dot >> currMinor >> dot >> currPatch;

	// Only block if major version is newer (breaking changes)
	return fileMajor > currMajor;
}

// Present the migration notices (gate renames, the decoder-width fix) from a
// load in one dialog, instead of a modal popup per affected gate.
static void showMigrationNotices(const std::vector<cl::MigrationNotice> &notices) {
	if (notices.empty()) return;

	bool anyWarning = false;
	wxString msg;
	for (const cl::MigrationNotice &n : notices) {
		if (n.severity == cl::Severity::Warning) anyWarning = true;
		if (n.severity == cl::Severity::Warning) msg << wxT("Warning: ");
		msg << wxString::FromUTF8(n.summary.c_str()) << wxT("\n");
		if (!n.detail.empty())
			msg << wxT("    ") << wxString::FromUTF8(n.detail.c_str()) << wxT("\n");
		msg << wxT("\n");
	}
	wxMessageBox(msg, wxT("Circuit Updated"),
	             wxOK | (anyWarning ? wxICON_EXCLAMATION : wxICON_INFORMATION));
}

// Pull the <version> value out of a legacy document (if present) for the
// newer-major-version guard. Returns "" when there is no version tag.
static string extractVersion(const string &text) {
	const string open = "<version>", close = "</version>";
	size_t a = text.find(open);
	if (a == string::npos) return "";
	a += open.size();
	size_t b = text.find(close, a);
	if (b == string::npos) return "";
	return text.substr(a, b - a);
}

vector<GUICanvas*> CircuitParse::parseFile() {
	// Read the whole file; the format library detects v1/v2/v3, skips the v2
	// decoy, runs migrations, and hands back a plain circuit model plus notices.
	ifstream in(fileName.c_str(), ios::in | ios::binary);
	ostringstream buf;
	buf << in.rdbuf();
	string text = buf.str();

	// Keep old builds of Cedar Logic from opening newer, incompatible files.
	string version = extractVersion(text);
	if (!version.empty() && hasBreakingVersion(version, VERSION_NUMBER_STRING()) &&
	    !renderMode().headlessRender) {
		wxMessageBox("This file was made with a newer version of Cedar Logic. "
			"Go to 'Help\\Download Latest Version...' to open this file."
			"Close CedarLogic without saving to avoid overwriting your work!!!", "Version Error!");
		return gCanvases;
	}

	cl::LoadResult loaded;
	try {
		loaded = cl::loadCircuit(text);
	} catch (const std::exception &e) {
		wxMessageBox(wxString("Could not read this circuit file:\n") + e.what(),
		             "Load Error", wxOK | wxICON_ERROR);
		return gCanvases;
	}
	switch (loaded.source) {
	case cl::SourceFormat::XmlV1: loadedFormatCode = 1; break;
	case cl::SourceFormat::XmlV2: loadedFormatCode = 2; break;
	case cl::SourceFormat::SexprV3: loadedFormatCode = 3; break;
	default: loadedFormatCode = 0; break;
	}

	applyCircuitFile(loaded.file);
	if (!renderMode().headlessRender) showMigrationNotices(loaded.notices);

	gCanvas->getCircuit()->getOscope()->UpdateMenu();
	return gCanvases;
}

// Build the GUI from a parsed circuit model: one canvas per page, every gate
// (which in turn creates and connects its wires), then each wire's routed shape.
static cl::GateDef toGateDef(const LibraryGate &lg);
static LibraryGate fromGateDef(const cl::GateDef &d);

void CircuitParse::applyCircuitFile(const cl::CircuitFile &cf) {
	// If no library was loaded, then we can't make gates from one.
	if (gateLibrary().libraries.size() == 0) return;

	// For any gate the current library no longer has, rebuild it from the copy
	// embedded in the file so the circuit still loads with its shape and pins
	// instead of a blank gate. A gate still present in the library is left
	// untouched (the `continue`) -- the live library wins, so this only fills in
	// removed gates, it does not override a changed one.
	for (const cl::GateDef &d : cf.usedGates) {
		if (gateLibrary().gateNameToLibrary.find(d.name) != gateLibrary().gateNameToLibrary.end()) continue;
		LibraryGate lg = fromGateDef(d);
		gateLibrary().libraries["embedded"][d.name] = lg;
		gateLibrary().gateNameToLibrary[d.name] = "embedded";
		// Also register with libParser, which parseGateToSend queries for the gate's
		// logic type + hotspots. Without this the core gate is never created and the
		// wires that connect to it crash.
		gateLibrary().libParser.addGate("embedded", lg);
	}

	for (const cl::Page &pg : cf.pages) {
		// Reuse the canvas for this page index, or grow the set to reach it.
		if (pg.index > (int)(gCanvases.size() - 1)) {
			gCanvas = new GUICanvas(gCanvases[0]->GetParent(), gCanvases[0]->getCircuit(),
			                        wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
			gCanvases.push_back(gCanvas);
		} else {
			gCanvas = gCanvases[pg.index];
		}

		if (pg.hasViewport) {
			gCanvas->setViewport(GLPoint2f(pg.viewTopLeft.x, pg.viewTopLeft.y),
			                     GLPoint2f(pg.viewBottomRight.x, pg.viewBottomRight.y));
		}

		// A gate's pin connections live on the wires; collect them per gate so a
		// gate is created with the same (pin -> wire ids) list the old gate-side
		// <input>/<output> blocks carried. Direction is irrelevant here — the old
		// loader handled inputs and outputs identically.
		std::unordered_map<string, vector<gateConnector>> connectorsByGate;
		for (const cl::WireInstance &w : pg.wires) {
			vector<IDType> wireIds;
			for (const string &id : w.ids) wireIds.push_back(strtoull(id.c_str(), nullptr, 10));
			std::set<std::pair<string, string>> seen; // distinct (gate, pin) endpoints
			for (const cl::WireSegment &s : w.segments) {
				for (const cl::WireConn &c : s.connects) {
					if (!seen.insert(std::make_pair(c.gateUuid, c.pin)).second) continue;
					gateConnector gc;
					gc.connectionID = c.pin;
					gc.wireIds = wireIds;
					connectorsByGate[c.gateUuid].push_back(gc);
				}
			}
		}

		// Create every gate (and, through its connectors, its wires).
		for (const cl::GateInstance &g : pg.gates) {
			vector<parameter> params;
			for (const cl::Param &p : g.params) params.push_back(parameter(p.name, p.value, p.gui));
			ostringstream angle;
			angle << g.angle;
			params.push_back(parameter("angle", angle.str(), true)); // model lifts angle out of params

			ostringstream position;
			position.precision(12); // don't lose coordinate precision in the double->text hop
			position << g.at.x << "," << g.at.y;

			vector<gateConnector> outputs; // everything passed as inputs; see note above
			parseGateToSend(g.libName, g.uuid, position.str(),
			                connectorsByGate[g.uuid], outputs, params);
		}

		// Lay in each wire's routed shape now that its gates and wire exist.
		for (const cl::WireInstance &w : pg.wires) applyWireShape(w);
	}
}

// Rebuild one wire's segment tree from the model and set it on the guiWire,
// mirroring what the old per-wire XML walk did (minus the parsing).
void CircuitParse::applyWireShape(const cl::WireInstance &w) {
	vector<IDType> ids;
	for (const string &id : w.ids) ids.push_back(strtoull(id.c_str(), nullptr, 10));
	if (ids.empty()) return;

	GUICircuit *gCircuit = gCanvas->getCircuit();
	// The wire must already exist (it was created while connecting its gates).
	if (gCircuit->getWires()->find(ids.front()) == gCircuit->getWires()->end()) return;

	map<long, wireSegment> shape;
	for (const cl::WireSegment &ms : w.segments) {
		wireSegment seg;
		seg.verticalSeg = ms.vertical;
		seg.id = strtol(ms.id.c_str(), nullptr, 10);
		seg.begin = GLPoint2f(ms.begin.x, ms.begin.y);
		seg.end = GLPoint2f(ms.end.x, ms.end.y);
		seg.calcBBox();
		for (const cl::WireConn &c : ms.connects) {
			wireConnection nwc;
			nwc.gid = strtoul(c.gateUuid.c_str(), nullptr, 10);
			nwc.connection = c.pin;
			seg.connections.push_back(nwc);
		}
		for (const cl::Intersection &x : ms.intersections)
			seg.intersects[(GLfloat)x.at].push_back(strtol(x.segment.c_str(), nullptr, 10));
		shape[seg.id] = seg;
	}

	guiWire *wire = (*(gCircuit->getWires()))[ids.front()];
	wire->setIDs(ids);
	wire->setSegmentMap(shape);
}

void CircuitParse::parseGateToSend(string type, string ID, string position, vector < gateConnector > &inputs, vector < gateConnector > &outputs, vector < parameter > &params) {
	// If no library was loaded, then don't try to make a gate from one
	if (gateLibrary().libraries.size() == 0) return;
	ostringstream oss;
	// Check the gate ID to see if it is taken
	long id;
	float x, y;
	istringstream issb(ID);
	issb >> id;
	
	string logicType = gateLibrary().libParser.getGateLogicType( type );
	if ( logicType.size() > 0 )
		gCanvas->getCircuit()->sendMessageToCore(klsMessage::Message(klsMessage::MT_CREATE_GATE, new klsMessage::Message_CREATE_GATE(gateLibrary().libraries[gateLibrary().gateNameToLibrary[type]][type].logicType, id)));
	// Create gate for GUI
	istringstream issa(position.substr(0,position.find(",")+1));
	issa >> x;
	issa.str(position.substr(position.find(",")+1,position.size()-position.find(",")-1));
	issa >> y;
	guiGate* newGate = gCanvas->getCircuit()->createGate( type, id, true );
	if (newGate == NULL) return; // IN CASE OF ERROR
	gCanvas->insertGate(id, newGate, x, y);
	for (unsigned int i = 0; i < params.size(); i++) {
		if (!(params[i].isGUI)) {
			newGate->setLogicParam( params[i].paramName, params[i].paramValue );
			gCanvas->getCircuit()->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, new klsMessage::Message_SET_GATE_PARAM(id, params[i].paramName, params[i].paramValue)));
		} else newGate->setGUIParam( params[i].paramName, params[i].paramValue );
	}
	if( logicType.size() > 0 ) {
		// Loop through the hotspots and pass logic core hotspot settings:
		LibraryGate libGate;
		gateLibrary().libParser.getGate(type, libGate);
		for( unsigned int i = 0; i < libGate.hotspots.size(); i++ ) {

			// Send the isInverted message:
			if( libGate.hotspots[i].isInverted ) {
				if ( libGate.hotspots[i].isInput ) {
					gCanvas->getCircuit()->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_INPUT_PARAM, new klsMessage::Message_SET_GATE_INPUT_PARAM(id, libGate.hotspots[i].name, "INVERTED", "TRUE")));
				} else {
					gCanvas->getCircuit()->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_OUTPUT_PARAM, new klsMessage::Message_SET_GATE_OUTPUT_PARAM(id, libGate.hotspots[i].name, "INVERTED", "TRUE")));
				}
			}

			// Send the logicEInput message:
			if( libGate.hotspots[i].logicEInput != "" ) {
				if ( libGate.hotspots[i].isInput ) {
					gCanvas->getCircuit()->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_INPUT_PARAM, new klsMessage::Message_SET_GATE_INPUT_PARAM(id, libGate.hotspots[i].name, "E_INPUT", libGate.hotspots[i].logicEInput)));
				} else {
					gCanvas->getCircuit()->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_OUTPUT_PARAM, new klsMessage::Message_SET_GATE_OUTPUT_PARAM(id, libGate.hotspots[i].name, "E_INPUT", libGate.hotspots[i].logicEInput)));
				}
			}
		} // for( loop through the hotspots )
	} // if( logic type is non-null )


	// Connect inputs and outputs.
	GUICircuit *gCircuit = gCanvas->getCircuit();
	for (unsigned int i = 0; i < inputs.size(); i++) {

		guiWire *wire = gCircuit->createWire(inputs[i].wireIds);

		cmdConnectWire::sendMessagesToConnect(gCircuit, wire->getID(),
			newGate->getID(), inputs[i].connectionID, true);

		gCanvas->insertWire(wire);
	}
	for (unsigned int i = 0; i < outputs.size(); i++) {

		guiWire *wire = gCircuit->createWire(outputs[i].wireIds);

		cmdConnectWire::sendMessagesToConnect(gCircuit, wire->getID(),
			newGate->getID(), outputs[i].connectionID, true);

		gCanvas->insertWire(wire);
	}
}

// ----------------------------------------------------------- v3 writer ------
// The inverse of applyCircuitFile: read the GUI back into a plain circuit model
// so the format library can serialize it. Mirrors what saveGate/saveWire persist.

static cl::GateInstance buildGate(guiGate *g) {
	cl::GateInstance gi;
	gi.uuid = to_string(g->getID());
	gi.libName = g->getLibraryGateName();
	float x, y;
	g->getGLcoords(x, y);
	gi.at = { x, y };

	// angle is a first-class field in the model, not a GUI param.
	for (const auto &p : *g->getAllGUIParams()) {
		if (p.first == "angle") { gi.angle = atof(p.second.c_str()); continue; }
		gi.params.push_back({ p.first, p.second, true });
	}
	// Logic params, skipping FILE_IN/FILE_OUT (runtime paths, as saveGate does).
	LibraryGate lg = gateLibrary().libraries[g->getLibraryName()][g->getLibraryGateName()];
	for (const auto &p : *g->getAllLogicParams()) {
		bool isFile = false;
		for (size_t i = 0; i < lg.dlgParams.size() && !isFile; i++)
			if (!lg.dlgParams[i].isGui &&
			    (lg.dlgParams[i].type == "FILE_IN" || lg.dlgParams[i].type == "FILE_OUT") &&
			    lg.dlgParams[i].name == p.first)
				isFile = true;
		if (!isFile) gi.params.push_back({ p.first, p.second, false });
	}
	// Gate-type-specific params (e.g. RAM memory contents).
	vector< pair<string, string> > extra;
	g->getTypeSpecificParams(extra);
	for (const auto &p : extra) gi.params.push_back({ p.first, p.second, false });
	return gi;
}

static cl::WireInstance buildWire(guiWire *w) {
	cl::WireInstance wi;
	for (IDType id : w->getIDs()) wi.ids.push_back(to_string(id));
	map<long, wireSegment> segMap = w->getSegmentMap();
	for (const auto &entry : segMap) {
		const wireSegment &s = entry.second;
		cl::WireSegment ws;
		ws.id = to_string(s.id);
		ws.vertical = s.verticalSeg;
		ws.begin = { s.begin.x, s.begin.y };
		ws.end = { s.end.x, s.end.y };
		for (const wireConnection &c : s.connections)
			ws.connects.push_back({ to_string(c.gid), c.connection });
		for (const auto &isect : s.intersects)
			for (long segId : isect.second)
				ws.intersections.push_back({ (double)isect.first, to_string(segId) });
		wi.segments.push_back(ws);
	}
	return wi;
}

// LibraryGate <-> the format's embedded GateDef. Lets a saved circuit carry a
// copy of every gate it uses, so it still opens if a gate is later removed from
// the library.
static cl::GateDef toGateDef(const LibraryGate &lg) {
	cl::GateDef d;
	d.name = lg.gateName;
	d.caption = lg.caption;
	d.guiType = lg.guiType;
	d.logicType = lg.logicType;
	for (const lgHotspot &h : lg.hotspots)
		d.hotspots.push_back({ h.name, h.isInput, h.x, h.y, h.isInverted, h.logicEInput, h.busLines });
	for (const lgLine &l : lg.shape)
		d.shape.push_back({ l.x1, l.y1, l.x2, l.y2, l.isLabel });
	for (const lgArc &a : lg.arcs)
		d.arcs.push_back({ a.cx, a.cy, a.r, a.startDeg, a.sweepDeg, a.isLabel });
	for (const lgCircle &c : lg.circles)
		d.circles.push_back({ c.cx, c.cy, c.r, c.segs, c.isLabel });
	for (const lgDlgParam &p : lg.dlgParams)
		d.dlgParams.push_back({ p.textLabel, p.name, p.type, p.isGui, p.Rmin, p.Rmax });
	for (const auto &kv : lg.guiParams) d.params.push_back({ kv.first, kv.second, true });
	for (const auto &kv : lg.logicParams) d.params.push_back({ kv.first, kv.second, false });
	return d;
}

static LibraryGate fromGateDef(const cl::GateDef &d) {
	LibraryGate lg;
	lg.gateName = d.name;
	lg.caption = d.caption;
	lg.guiType = d.guiType;
	lg.logicType = d.logicType;
	for (const cl::HotspotDef &h : d.hotspots)
		lg.hotspots.push_back(lgHotspot(h.name, h.isInput, (float)h.x, (float)h.y, h.inverted, h.eInput, h.busLines));
	for (const cl::LineDef &l : d.shape)
		lg.shape.push_back(lgLine((float)l.x1, (float)l.y1, (float)l.x2, (float)l.y2, l.isLabel));
	for (const cl::ArcDef &a : d.arcs)
		lg.arcs.push_back(lgArc((float)a.cx, (float)a.cy, (float)a.r, (float)a.startDeg, (float)a.sweepDeg, a.isLabel));
	for (const cl::CircleDef &c : d.circles)
		lg.circles.push_back(lgCircle((float)c.cx, (float)c.cy, (float)c.r, c.segs, c.isLabel));
	for (const cl::DlgParamDef &p : d.dlgParams)
		lg.dlgParams.push_back(lgDlgParam(p.label, p.name, p.type, p.isGui, p.rMin, p.rMax));
	for (const cl::Param &p : d.params) {
		if (p.gui) lg.guiParams[p.name] = p.value;
		else lg.logicParams[p.name] = p.value;
	}
	return lg;
}

static cl::CircuitFile buildCircuitFile(vector<GUICanvas*> &glc) {
	cl::CircuitFile cf;
	cf.formatVersion = 3;
	cf.generator = string("CedarLogic ") + VERSION_NUMBER_STRING();
	for (unsigned int i = 0; i < glc.size(); i++) {
		cl::Page pg;
		pg.index = (int)i;
		GLPoint2f topLeft, bottomRight;
		glc[i]->getViewport(topLeft, bottomRight);
		pg.hasViewport = true;
		pg.viewTopLeft = { topLeft.x, topLeft.y };
		pg.viewBottomRight = { bottomRight.x, bottomRight.y };
		for (const auto &entry : *glc[i]->getGateList())
			pg.gates.push_back(buildGate(entry.second));
		for (const auto &entry : *glc[i]->getWireList())
			if (entry.second != nullptr) pg.wires.push_back(buildWire(entry.second));
		cf.pages.push_back(std::move(pg));
	}

	// Embed a copy of every gate definition the circuit uses.
	std::set<string> embedded;
	for (const cl::Page &pg : cf.pages)
		for (const cl::GateInstance &g : pg.gates) {
			if (!embedded.insert(g.libName).second) continue;
			auto nameIt = gateLibrary().gateNameToLibrary.find(g.libName);
			if (nameIt == gateLibrary().gateNameToLibrary.end()) continue;
			auto libIt = gateLibrary().libraries.find(nameIt->second);
			if (libIt == gateLibrary().libraries.end()) continue;
			auto defIt = libIt->second.find(g.libName);
			if (defIt != libIt->second.end()) cf.usedGates.push_back(toGateDef(defIt->second));
		}
	return cf;
}

bool CircuitParse::saveCircuitV3(string filename, vector< GUICanvas* > glc, unsigned int currPage) {
	return writeToFile(filename, cl::writeCircuitFile(buildCircuitFile(glc)));
}

bool CircuitParse::saveCircuit(string filename, vector< GUICanvas* > glc, unsigned int currPage) {
	ostringstream* ossCircuit = new ostringstream();

	// This is a sentinal circuit definition that is ignored by Cedar Logic 2.0 and newer.
	// Older versions of Cedar Logic will read this instead of the actual Circuit data.
	// This circuit decribes two labels with error messages.
	// The second label has a link to the download for the latest version of Cedar Logic.
	// I acknowledge that this is a hack...
	// Versions of Cedar Logic 2.0 and newer have a <version> tag.
	*ossCircuit << R"===(
<circuit>
<CurrentPage>0</CurrentPage>
<page 0>
<PageViewport>-32.95,39.6893,61.95,-63.2229</PageViewport>
<gate>
<ID>2</ID>
<type>AA_LABEL</type>
<position>14.5,-13</position>
<gparam>LABEL_TEXT Go to https://cedar.to/vjyQw7 to download the latest version!</gparam>
<gparam>TEXT_HEIGHT 2</gparam>
<gparam>angle 0.0</gparam></gate>
<gate>
<ID>3</ID>
<type>AA_LABEL</type>
<position>14.5,-9.5</position>
<gparam>LABEL_TEXT Error: This file was made with a newer version of Cedar Logic!</gparam>
<gparam>TEXT_HEIGHT 2</gparam>
<gparam>angle 0.0</gparam></gate></page 0>
</circuit>
<throw_away></throw_away>

	)===";

	mParse = new XMLParser(ossCircuit);

	mParse->openTag("version");
	mParse->writeTag("version", VERSION_NUMBER_STRING());
	mParse->closeTag("version");

	mParse->openTag("circuit");
	unordered_map < unsigned long, guiGate* >* gateList;
	unordered_map < unsigned long, guiWire* >* wireList;

	// Save which page was current:
	//	NOTE: currently this tag is not implemented
	mParse->openTag("CurrentPage");
	ostringstream oss;
	oss << currPage;
	mParse->writeTag("CurrentPage", oss.str());
	mParse->closeTag("CurrentPage");

	for (unsigned int i = 0; i < glc.size(); i++) {
		ostringstream oss;
		oss << "page " << i;
		string pageNumber = oss.str();
		mParse->openTag(pageNumber);

		// Save the page's last viewport
		mParse->openTag("PageViewport");
		oss.str("");
		oss.clear();
		GLPoint2f topLeft, bottomRight;
		glc[i]->getViewport(topLeft, bottomRight);
		oss << topLeft.x << "," << topLeft.y << "," << bottomRight.x << "," << bottomRight.y;
		mParse->writeTag("PageViewport", oss.str());
		mParse->closeTag("PageViewport");

		gateList = glc[i]->getGateList();
		wireList = glc[i]->getWireList();
		unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList->begin();
		while (thisGate != gateList->end()) {
			(thisGate->second)->saveGate(mParse);
			thisGate++;
		}
		
		unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList->begin();
		while (thisWire != wireList->end()) {
			if (thisWire->second != nullptr) {
				(thisWire->second)->saveWire(mParse);
			}
			thisWire++;
		}
		
		mParse->closeTag(pageNumber);
	}
	
	mParse->closeTag("circuit");

	// Clear any previous error
	lastError = "";

	// Attempt to open file for writing
	errno = 0;  // Clear errno before operation
	ofstream outfile(filename.c_str());
	if (!outfile.good()) {
		int errnum = errno;
		if (errnum == EACCES || errnum == EPERM) {
			lastError = "Permission denied. You don't have write access to this location.";
		} else if (errnum == ENOSPC) {
			lastError = "Disk full. Free up space and try again.";
		} else if (errnum == EROFS) {
			lastError = "Read-only filesystem. Choose a different location.";
		} else if (errnum == ENOENT) {
			lastError = "Directory doesn't exist. Check the file path.";
		} else if (errnum != 0) {
			lastError = string("Cannot open file: ") + strerror(errnum);
		} else {
			lastError = "Cannot open file for writing.";
		}
		return false;
	}

	// Write the circuit data
	errno = 0;
	outfile << ossCircuit->str();
	if (outfile.fail()) {
		int errnum = errno;
		outfile.close();
		if (errnum == ENOSPC) {
			lastError = "Disk full while writing. The file may be incomplete.";
		} else if (errnum == EIO) {
			lastError = "I/O error while writing. Check your disk or network connection.";
		} else if (errnum != 0) {
			lastError = string("Write failed: ") + strerror(errnum);
		} else {
			lastError = "Write operation failed.";
		}
		return false;
	}

	// Close the file (this flushes buffers and may reveal errors)
	errno = 0;
	outfile.close();
	if (outfile.fail()) {
		int errnum = errno;
		if (errnum == ENOSPC) {
			lastError = "Disk full while closing file. The file may be incomplete.";
		} else if (errnum == EIO) {
			lastError = "I/O error while closing file. Data may not be saved correctly.";
		} else if (errnum == EDQUOT) {
			lastError = "Disk quota exceeded. Free up space or request more quota.";
		} else if (errnum != 0) {
			lastError = string("Error closing file: ") + strerror(errnum);
		} else {
			lastError = "Failed to close file properly. Data may not be saved.";
		}
		return false;
	}

	return true;
}

// Save in v1.x compatible format (no version tag, no sentinel, single wire IDs)
// Returns false if circuit uses bus features that can't be fully represented
bool CircuitParse::saveCircuitLegacy(string filename, vector< GUICanvas* > glc, unsigned int currPage) {
	bool hasBusFeatures = false;

	// Check if any wire has multiple IDs (bus feature)
	for (unsigned int i = 0; i < glc.size(); i++) {
		unordered_map< unsigned long, guiWire* >* wireList = glc[i]->getWireList();
		unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList->begin();
		while (thisWire != wireList->end()) {
			if (thisWire->second != nullptr && thisWire->second->getIDs().size() > 1) {
				hasBusFeatures = true;
				break;
			}
			thisWire++;
		}
		if (hasBusFeatures) break;
	}

	ostringstream* ossCircuit = new ostringstream();
	mParse = new XMLParser(ossCircuit);

	// v1.x format: no version tag, no sentinel - just circuit directly
	mParse->openTag("circuit");
	unordered_map < unsigned long, guiGate* >* gateList;
	unordered_map < unsigned long, guiWire* >* wireList;

	// Save which page was current
	mParse->openTag("CurrentPage");
	ostringstream oss;
	oss << currPage;
	mParse->writeTag("CurrentPage", oss.str());
	mParse->closeTag("CurrentPage");

	for (unsigned int i = 0; i < glc.size(); i++) {
		ostringstream oss;
		oss << "page " << i;
		string pageNumber = oss.str();
		mParse->openTag(pageNumber);

		// Save the page's last viewport
		mParse->openTag("PageViewport");
		oss.str("");
		oss.clear();
		GLPoint2f topLeft, bottomRight;
		glc[i]->getViewport(topLeft, bottomRight);
		oss << topLeft.x << "," << topLeft.y << "," << bottomRight.x << "," << bottomRight.y;
		mParse->writeTag("PageViewport", oss.str());
		mParse->closeTag("PageViewport");

		gateList = glc[i]->getGateList();
		wireList = glc[i]->getWireList();
		unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList->begin();
		while (thisGate != gateList->end()) {
			// Use legacy save method (single wire IDs)
			(thisGate->second)->saveGateLegacy(mParse);
			thisGate++;
		}

		unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList->begin();
		while (thisWire != wireList->end()) {
			if (thisWire->second != nullptr) {
				// Use legacy save method (single wire ID)
				(thisWire->second)->saveWireLegacy(mParse);
			}
			thisWire++;
		}

		mParse->closeTag(pageNumber);
	}

	mParse->closeTag("circuit");

	// Clear any previous error
	lastError = "";

	// Attempt to open file for writing
	errno = 0;  // Clear errno before operation
	ofstream outfile(filename.c_str());
	if (!outfile.good()) {
		int errnum = errno;
		if (errnum == EACCES || errnum == EPERM) {
			lastError = "Permission denied. You don't have write access to this location.";
		} else if (errnum == ENOSPC) {
			lastError = "Disk full. Free up space and try again.";
		} else if (errnum == EROFS) {
			lastError = "Read-only filesystem. Choose a different location.";
		} else if (errnum == ENOENT) {
			lastError = "Directory doesn't exist. Check the file path.";
		} else if (errnum != 0) {
			lastError = string("Cannot open file: ") + strerror(errnum);
		} else {
			lastError = "Cannot open file for writing.";
		}
		delete mParse;
		mParse = nullptr;
		delete ossCircuit;
		return false;
	}

	// Write the circuit data
	errno = 0;
	outfile << ossCircuit->str();
	if (outfile.fail()) {
		int errnum = errno;
		outfile.close();
		if (errnum == ENOSPC) {
			lastError = "Disk full while writing. The file may be incomplete.";
		} else if (errnum == EIO) {
			lastError = "I/O error while writing. Check your disk or network connection.";
		} else if (errnum != 0) {
			lastError = string("Write failed: ") + strerror(errnum);
		} else {
			lastError = "Write operation failed.";
		}
		delete mParse;
		mParse = nullptr;
		delete ossCircuit;
		return false;
	}

	// Close the file (this flushes buffers and may reveal errors)
	errno = 0;
	outfile.close();
	if (outfile.fail()) {
		int errnum = errno;
		if (errnum == ENOSPC) {
			lastError = "Disk full while closing file. The file may be incomplete.";
		} else if (errnum == EIO) {
			lastError = "I/O error while closing file. Data may not be saved correctly.";
		} else if (errnum == EDQUOT) {
			lastError = "Disk quota exceeded. Free up space or request more quota.";
		} else if (errnum != 0) {
			lastError = string("Error closing file: ") + strerror(errnum);
		} else {
			lastError = "Failed to close file properly. Data may not be saved.";
		}
		delete mParse;
		mParse = nullptr;
		delete ossCircuit;
		return false;
	}

	delete mParse;
	mParse = nullptr;
	delete ossCircuit;

	// If we got here, file was saved successfully
	// Return false only if there are bus features (backward compatibility warning)
	if (hasBusFeatures) {
		lastError = "Warning: Circuit uses bus features that cannot be represented in v1.x format.";
	}
	return !hasBusFeatures;
}

// Write text to disk, translating errno into a friendly lastError on failure.
//
// The write is atomic: the text goes to a temporary file beside the target and
// is renamed over it only once it is safely on disk. Truncating the target and
// streaming into it -- what this used to do -- means a crash, a full disk or a
// pulled USB stick partway through destroys the file the user already had, with
// no copy anywhere. With a rename the save either happens completely or leaves
// the previous file exactly as it was.
//
// wxTempFile does the fiddly parts: the temporary lands in the SAME directory
// (so the rename cannot cross filesystems and fall back to a copy), it inherits
// the original's permissions on Unix, Flush() is fsync(), and Commit() is
// rename()/MoveFileEx().
bool CircuitParse::writeToFile(const string &filename, const string &text) {
	lastError = "";

	// Translate errno into something a student can act on. Shared by every step
	// below; `stage` names what failed when errno has nothing useful to add.
	auto describe = [this](int errnum, const char* stage) {
		if (errnum == EACCES || errnum == EPERM) {
			lastError = "Permission denied. You don't have write access to this location.";
		} else if (errnum == ENOSPC) {
			lastError = "Disk full. Free up space and try again.";
		} else if (errnum == EDQUOT) {
			lastError = "Disk quota exceeded. Free up space or request more quota.";
		} else if (errnum == EROFS) {
			lastError = "Read-only filesystem. Choose a different location.";
		} else if (errnum == ENOENT) {
			lastError = "Directory doesn't exist. Check the file path.";
		} else if (errnum == EIO) {
			lastError = "I/O error. Check your disk or network connection.";
		} else if (errnum != 0) {
			lastError = string(stage) + ": " + strerror(errnum);
		} else {
			lastError = stage;
		}
	};

	errno = 0;
	wxTempFile out;
	if (!out.Open(filename)) {
		describe(errno, "Cannot open file for writing");
		return false;
	}

	// Raw bytes, not the wxString overload: that one re-encodes through the
	// current locale, and a label with a non-ASCII character in it would not
	// survive the round trip byte for byte.
	errno = 0;
	if (!out.Write(text.data(), text.size())) {
		describe(errno, "Write operation failed");
		out.Discard();          // leave the original untouched
		return false;
	}

	// fsync before the rename: without it the rename can land while the new
	// file's contents are still only in the page cache, which after a power cut
	// leaves an intact-looking file full of nothing.
	errno = 0;
	if (!out.Flush()) {
		describe(errno, "Could not flush the file to disk");
		out.Discard();
		return false;
	}

	errno = 0;
	if (!out.Commit()) {
		describe(errno, "Could not replace the existing file");
		return false;   // Commit() cleans up its own temporary
	}

	return true;
}
