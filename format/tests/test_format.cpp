#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "circuit_file_io.hpp"
#include "sexpr.hpp"

using namespace cl;

TEST_CASE("S-expression round-trips through write then parse") {
	SNode n = SNode::list();
	n.add(SNode::sym("gate"));
	n.add(SNode::str("AA_AND2"));
	SNode at = SNode::list();
	at.add(SNode::sym("at"));
	at.add(SNode::sym("-8"));
	at.add(SNode::sym("0"));
	n.add(at);
	CHECK(parseSexpr(writeSexpr(n)) == n);
}

TEST_CASE("Strings with special characters survive (the old-format corruption is gone)") {
	// #, &, and < each silently destroyed data in the hand-rolled pseudo-XML;
	// quotes and backslashes need real escaping.
	for (const std::string &v :
	     { "Pin #3", "A & B", "a<b>c", "quote\"here", "back\\slash", "with spaces", "" }) {
		SNode round = parseSexpr(writeSexpr(SNode::str(v)));
		CHECK(round.kind == SNode::Kind::String);
		CHECK(round.text == v);
	}
}

TEST_CASE("CircuitFile round-trips through the v3 format") {
	CircuitFile cf;
	cf.generator = "CedarLogic test";

	Page pg;
	pg.index = 0;
	pg.hasViewport = true;
	pg.viewTopLeft = { -113.179, 16.25 };
	pg.viewBottomRight = { -24.4457, -30.3 };

	GateInstance toggle;
	toggle.uuid = "3f9a";
	toggle.libName = "AA_TOGGLE";
	toggle.at = { -8, 0 };
	toggle.params = { { "OUTPUT_NUM", "1" } };
	pg.gates.push_back(toggle);

	GateInstance andGate;
	andGate.uuid = "b21c";
	andGate.libName = "AA_AND2";
	andGate.at = { 12, -4 };
	andGate.angle = 90;
	andGate.params = { { "INPUT_BITS", "2" } };
	pg.gates.push_back(andGate);

	WireInstance wire;
	wire.ids = { "77e0" };
	WireSegment s0;
	s0.id = "0";
	s0.vertical = false;
	s0.begin = { -5, 0 };
	s0.end = { 3, 0 };
	s0.connects = { { "3f9a", "OUT_0" } };
	s0.intersections = { { 3, "1" } }; // meets segment 1 at x=3
	WireSegment s1;
	s1.id = "1";
	s1.vertical = true;
	s1.begin = { 3, 0 };
	s1.end = { 3, 1 };
	s1.connects = { { "b21c", "IN_0" } };
	s1.intersections = { { 0, "0" } };
	wire.segments = { s0, s1 };
	pg.wires.push_back(wire);

	cf.pages.push_back(pg);

	std::string text = writeCircuitFile(cf);
	CircuitFile back = readCircuitFile(text);

	CHECK(back == cf);                          // model survives the round trip
	CHECK(writeCircuitFile(back) == text);      // and the text is stable
}

TEST_CASE("A param value with special characters round-trips through the file") {
	CircuitFile cf;
	Page pg;
	GateInstance g;
	g.uuid = "1";
	g.libName = "AA_LABEL";
	g.params = { { "text", "R1 & C2 (#3) \"load\"" } };
	pg.gates.push_back(g);
	cf.pages.push_back(pg);

	CircuitFile back = readCircuitFile(writeCircuitFile(cf));
	REQUIRE(back.pages.size() == 1);
	REQUIRE(back.pages[0].gates.size() == 1);
	CHECK(back.pages[0].gates[0].params[0].value == "R1 & C2 (#3) \"load\"");
}

TEST_CASE("Reading rejects an unsupported format version") {
	CHECK_THROWS(readCircuitFile("(cedarlogic (version 2) (generator \"old\"))"));
}

TEST_CASE("Reading rejects a non-cedarlogic document") {
	CHECK_THROWS(readCircuitFile("(something (version 3))"));
}

TEST_CASE("Reading rejects malformed input") {
	CHECK_THROWS(readCircuitFile("(cedarlogic (version 3"));  // unbalanced
}
