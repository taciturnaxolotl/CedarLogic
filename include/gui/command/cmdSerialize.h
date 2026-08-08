#pragma once

// Typed, wxWidgets-free serialization for the undo/clipboard command formats.
//
// The command classes hand-roll their wire format inline: an ostringstream in
// toString(), an istringstream in the string constructor, coupling the format to
// the GUI-bound command objects and leaving it untestable. These plain structs
// plus emit()/parse() own that format for the commands migrated so far, so it
// can be round-trip tested headless; keyword() owns the leading token the paste
// dispatcher switches on (replacing hand-counted substr offsets). The migrated
// commands delegate here, so the on-the-wire bytes are unchanged. Every command
// that carries a wire format now lives here; nothing serializes inline anymore.
#ifndef CMD_SERIALIZE_H
#define CMD_SERIALIZE_H

#include <map>
#include <string>
#include <vector>
#include "logic_values.h" // IDType

namespace cmdser {

// "creategate <gid> <gateType> <x> <y>"
struct CreateGate {
	unsigned long gid = 0;
	std::string gateType;
	float x = 0.0f;
	float y = 0.0f;
};

// "connectwire <wireId> <gateId> <hotspot>"
struct ConnectWire {
	IDType wireId = 0;
	IDType gateId = 0;
	std::string hotspot;
};

// "movegate <gid> <startX> <startY> <endX> <endY>"
struct MoveGate {
	unsigned long gid = 0;
	float startX = 0.0f;
	float startY = 0.0f;
	float endX = 0.0f;
	float endY = 0.0f;
};

// "disconnectwire <wireId> <gateId> <hotspot>" (emit-only, like MoveGate).
struct DisconnectWire {
	IDType wireId = 0;
	IDType gateId = 0;
	std::string hotspot;
};

// "createwire <id>... connectwire <...> connectwire <...>": a variable-length
// list of wire ids followed by the two endpoint connections. The id list ends
// where the first "connectwire" keyword begins.
struct CreateWire {
	std::vector<IDType> wireIds;
	ConnectWire conn1;
	ConnectWire conn2;
};

// "setparams <gid> <#gui>,<#logic> <name> <value>\t...": a gid, a comma-joined
// count of each map, then "<name> <value>\t" per entry (GUI params first, then
// logic). Values may contain spaces, so a param ends at its tab, not whitespace.
// The maps are std::map, so entries emit in sorted-key order (deterministic).
struct SetParams {
	unsigned long gid = 0;
	std::map<std::string, std::string> guiParams;
	std::map<std::string, std::string> logicParams;
};

// One segment of a wire's routed shape, GUI/GL-free: orientation, id, its two
// endpoints, the gate connections landing on it (gid + hotspot name), and the
// intersections with other segments (key coordinate + segment id).
struct MoveWireSeg {
	long id = 0;
	bool vertical = false;
	float bx = 0, by = 0, ex = 0, ey = 0;
	std::vector<std::pair<unsigned long, std::string>> connections;
	std::vector<std::pair<float, long>> intersects;
};

// "movewire <wid> [vsegment|hsegment <id> <bx>,<by>,<ex>,<ey>
//   [connection <gid> <name>]* [isect <key> <segid>]*]* done"
// The nested per-segment records the GUI wrote by hand; segments emit in the
// wire's sorted segment-map order.
struct MoveWire {
	IDType wid = 0;
	std::vector<MoveWireSeg> segments;
};

std::string emit(const CreateGate &c);
std::string emit(const ConnectWire &c);
std::string emit(const MoveGate &c);
std::string emit(const DisconnectWire &c);
std::string emit(const CreateWire &c);
std::string emit(const SetParams &c);
std::string emit(const MoveWire &c);

// The leading keyword of a command line, e.g. "creategate 5 AND 1 2" -> "creategate".
// This is what the paste dispatcher matches on to pick a command.
std::string keyword(const std::string &line);

// Parse a whole command line (keyword included) into the struct, mirroring the
// original istringstream extraction. Returns false if the leading keyword does
// not match. The command constructors call these only after the dispatcher has
// matched via keyword(), so they ignore the result; standalone callers and the
// round-trip tests check it. (MoveGate is emit-only -- nothing parses it.)
bool parse(const std::string &line, CreateGate &out);
bool parse(const std::string &line, ConnectWire &out);
bool parse(const std::string &line, CreateWire &out);
bool parse(const std::string &line, SetParams &out);
bool parse(const std::string &line, MoveWire &out);

} // namespace cmdser

#endif // CMD_SERIALIZE_H
