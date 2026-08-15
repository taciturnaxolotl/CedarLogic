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
	std::vector<GateInstance> gates;
	std::vector<WireInstance> wires;
	bool operator==(const Page &o) const {
		return index == o.index && gates == o.gates && wires == o.wires;
	}
};

struct CircuitFile {
	int formatVersion = 3;
	std::string generator;
	std::vector<Page> pages;
	bool operator==(const CircuitFile &o) const {
		return formatVersion == o.formatVersion && generator == o.generator &&
		       pages == o.pages;
	}
};

} // namespace cl
