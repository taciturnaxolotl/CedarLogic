// Which GUI params a circuit file is allowed to carry.
//
// A gate's GUI params are a mix of two very different things: what the user did
// to this gate (the angle they turned it to, the label they typed) and how the
// library says every gate of that kind is drawn (its hit box, its lit-value
// box, a keypad's sixteen cell boxes). Saving used to write out both, so every
// circuit ever saved pinned the drawing boxes at whatever the library shipped
// that day. Retuning a gate then reached new circuits only: a resized keypad
// would draw its new digit grid while an old file kept clicking on the old one,
// silently, with no wire or gate visibly out of place.
//
// The rule that separates them is checked here against the real shipped gate
// library rather than a hand-made stand-in, because the whole value of the rule
// is that it classifies the gates CedarLogic actually has.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "GateLibrary.h"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#ifndef GATEDEFS_PATH
#define GATEDEFS_PATH "res/cl_gatedefs.xml"
#endif

namespace {

// Parse the shipped library once for the whole suite. Constructing the parser
// is what reads it -- gates land in the process-wide GateLibrary as they are
// parsed -- which is how MainFrame loads it too.
GateLibrary &library() {
	static bool loaded = false;
	if (!loaded) {
		std::ifstream f(GATEDEFS_PATH, std::ios::binary);
		REQUIRE_MESSAGE(f.good(), "missing gate library: " << GATEDEFS_PATH);
		std::ostringstream ss;
		ss << f.rdbuf();
		gateLibrary().libParser = LibraryParse(ss.str());
		loaded = true;
	}
	return gateLibrary();
}

const LibraryGate &gateNamed(const std::string &name) {
	static LibraryGate found;
	REQUIRE_MESSAGE(library().libParser.getGate(name, found),
	                "missing library gate: " << name);
	return found;
}

}  // namespace

TEST_CASE("the drawing boxes belong to the library") {
	// Geometry, not circuit data. No dialog in the app can edit these, so a
	// saved circuit has no business carrying them.
	CHECK(gateNamed("GA_LED").ownsGUIParam("LED_BOX"));
	CHECK(gateNamed("CC_PULSE").ownsGUIParam("CLICK_BOX"));
	CHECK(gateNamed("AA_REGISTER4").ownsGUIParam("VALUE_BOX"));
	CHECK(gateNamed("DD_KEYPAD_HEX").ownsGUIParam("KEYPAD_BOX_0"));
	CHECK(gateNamed("DD_KEYPAD_HEX").ownsGUIParam("KEYPAD_BOX_F"));
}

TEST_CASE("what the user set belongs to the user") {
	// A dialog exposes it, so someone may well have changed it: the file wins.
	CHECK_FALSE(gateNamed("CC_PULSE").ownsGUIParam("PULSE_WIDTH"));
	// The library says nothing about these -- they come from a gate's
	// constructor, or straight from the user -- so they are never its to take.
	CHECK_FALSE(gateNamed("AA_TOGGLE").ownsGUIParam("CLICK_BOX"));
	CHECK_FALSE(gateNamed("GA_LED").ownsGUIParam("angle"));
	CHECK_FALSE(gateNamed("GA_LED").ownsGUIParam("LABEL_TEXT"));
}

TEST_CASE("every param the library claims is a box, and nothing else is") {
	// The sweep that makes the cases above more than anecdotes: across the whole
	// library, the params the rule hands to the library are precisely the
	// drawing boxes, and nothing else is swept up with them.
	std::set<std::string> claimed, released;
	for (const auto &libEntry : library().libraries) {
		for (const auto &gateEntry : libEntry.second) {
			const LibraryGate &lg = gateEntry.second;
			for (const auto &p : lg.guiParams)
				(lg.ownsGUIParam(p.first) ? claimed : released).insert(p.first);
		}
	}
	REQUIRE_FALSE(claimed.empty());
	for (const std::string &name : claimed) {
		const bool isBox = name == "CLICK_BOX" || name == "LED_BOX" ||
		                   name == "VALUE_BOX" ||
		                   name.compare(0, 11, "KEYPAD_BOX_") == 0;
		CHECK_MESSAGE(isBox, "library claims a non-box param: " << name);
	}
	// PULSE_WIDTH is the one library-supplied param a dialog exposes, so it is
	// the proof that "the library supplies it" is not on its own the rule.
	CHECK(released.count("PULSE_WIDTH") == 1);
}
