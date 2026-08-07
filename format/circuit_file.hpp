#pragma once

#include <string>
#include <vector>

namespace cl {

// A plain, GUI- and core-independent description of a saved circuit: the seam
// the v3 .cdl format sits on. Reading a file produces one of these; writing
// consumes one. Applying it to the live GUI/core (or building one from them) is
// a separate pass, so the format can be loaded, validated, and tested headless.

struct XY {
	double x = 0, y = 0;
	bool operator==(const XY &o) const { return x == o.x && y == o.y; }
};

// A parameter is a name/value pair; values are strings at the file edge and
// parse into typed values (see the gate paramSchema) when applied. `gui`
// distinguishes a GUI param (legacy <gparam>) from a logic param (<lparam>).
struct Param {
	std::string name;
	std::string value;
	bool gui = false;
	bool operator==(const Param &o) const {
		return name == o.name && value == o.value && gui == o.gui;
	}
};

struct GateInstance {
	std::string uuid;
	std::string libName;   // library gate name, e.g. "AA_AND2"
	XY at;
	double angle = 0;
	std::vector<Param> params;
	bool operator==(const GateInstance &o) const {
		return uuid == o.uuid && libName == o.libName && at == o.at &&
		       angle == o.angle && params == o.params;
	}
};

// One endpoint on a wire segment: a gate (by uuid) and the pin (hotspot) on it.
struct WireConn {
	std::string gateUuid;
	std::string pin;
	bool operator==(const WireConn &o) const { return gateUuid == o.gateUuid && pin == o.pin; }
};

// A point where one segment meets another of the same wire — the branch topology.
// `at` is the coordinate along the segment (x for a horizontal segment, y for a
// vertical one); `segment` is the id of the segment met there.
struct Intersection {
	double at = 0;
	std::string segment;
	bool operator==(const Intersection &o) const { return at == o.at && segment == o.segment; }
};

// One straight run of a wire. Wires are a tree of these, joined at intersections;
// the whole structure is routing the user drew, so it is preserved verbatim.
struct WireSegment {
	std::string id;
	bool vertical = false;                    // authored orientation, not derived from a/b
	XY begin, end;                            // legacy invariant: begin <= end
	std::vector<WireConn> connects;           // gate pins landing on this segment
	std::vector<Intersection> intersections;  // joins to other segments of this wire
	bool operator==(const WireSegment &o) const {
		return id == o.id && vertical == o.vertical && begin == o.begin && end == o.end &&
		       connects == o.connects && intersections == o.intersections;
	}
};

// A wire: one or more IDs (a bus carries several lines) and its segment tree.
struct WireInstance {
	std::vector<std::string> ids;
	std::vector<WireSegment> segments;
	bool operator==(const WireInstance &o) const {
		return ids == o.ids && segments == o.segments;
	}
};

struct Page {
	int index = 0;
	bool hasViewport = false;         // the saved pan/zoom rectangle, if any
	XY viewTopLeft, viewBottomRight;
	std::vector<GateInstance> gates;
	std::vector<WireInstance> wires;
	bool operator==(const Page &o) const {
		return index == o.index && hasViewport == o.hasViewport &&
		       viewTopLeft == o.viewTopLeft && viewBottomRight == o.viewBottomRight &&
		       gates == o.gates && wires == o.wires;
	}
};

// A gate's definition, embedded in the file so a circuit stays self-contained:
// it opens correctly even if the library that defined the gate later changed or
// is gone. Mirrors the GUI's LibraryGate, kept GUI/core-independent.

struct HotspotDef {
	std::string name;
	bool isInput = true;
	double x = 0, y = 0;
	bool inverted = false;
	std::string eInput;
	int busLines = 1;
	bool operator==(const HotspotDef &o) const {
		return name == o.name && isInput == o.isInput && x == o.x && y == o.y &&
		       inverted == o.inverted && eInput == o.eInput && busLines == o.busLines;
	}
};

struct LineDef {
	double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	bool isLabel = false;
	bool operator==(const LineDef &o) const {
		return x1 == o.x1 && y1 == o.y1 && x2 == o.x2 && y2 == o.y2 && isLabel == o.isLabel;
	}
};

struct DlgParamDef {
	std::string label;
	std::string name;
	std::string type = "STRING";
	bool isGui = true;
	bool operator==(const DlgParamDef &o) const {
		return label == o.label && name == o.name && type == o.type && isGui == o.isGui;
	}
};

struct GateDef {
	std::string name;
	std::string caption;
	std::string guiType;
	std::string logicType;
	std::vector<HotspotDef> hotspots;
	std::vector<LineDef> shape;
	std::vector<DlgParamDef> dlgParams;
	std::vector<Param> params;   // gui vs logic distinguished by Param::gui
	bool operator==(const GateDef &o) const {
		return name == o.name && caption == o.caption && guiType == o.guiType &&
		       logicType == o.logicType && hotspots == o.hotspots && shape == o.shape &&
		       dlgParams == o.dlgParams && params == o.params;
	}
};

struct CircuitFile {
	int formatVersion = 3;
	std::string generator;
	std::vector<GateDef> usedGates;   // every gate the circuit uses, embedded
	std::vector<Page> pages;
	bool operator==(const CircuitFile &o) const {
		return formatVersion == o.formatVersion && generator == o.generator &&
		       usedGates == o.usedGates && pages == o.pages;
	}
};

} // namespace cl
