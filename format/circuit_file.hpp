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

// One endpoint of a wire: a gate (by uuid) and the pin name on it.
struct WireConn {
	std::string gateUuid;
	std::string pin;
	bool operator==(const WireConn &o) const { return gateUuid == o.gateUuid && pin == o.pin; }
};

// A straight segment of a wire's routed path. Wires branch, so routing is a set
// of segments (matching the legacy h/v segment tree), not a single polyline.
struct Segment {
	XY a, b;
	bool operator==(const Segment &o) const { return a == o.a && b == o.b; }
};

struct WireInstance {
	std::string uuid;
	std::vector<WireConn> connects;
	std::vector<Segment> route;   // the exact routed path, preserved verbatim
	bool operator==(const WireInstance &o) const {
		return uuid == o.uuid && connects == o.connects && route == o.route;
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
		return formatVersion == o.formatVersion && generator == o.generator && pages == o.pages;
	}
};

} // namespace cl
