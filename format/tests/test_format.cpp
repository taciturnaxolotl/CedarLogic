#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "circuit_file_io.hpp"
#include "migrate.hpp"
#include "sexpr.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

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

	GateInstance toggle;
	toggle.uuid = "1001";
	toggle.libName = "AA_TOGGLE";
	toggle.at = { -8, 0 };
	toggle.params = { { "OUTPUT_NUM", "1" } };
	pg.gates.push_back(toggle);

	GateInstance andGate;
	andGate.uuid = "1002";
	andGate.libName = "AA_AND2";
	andGate.at = { 12, -4 };
	andGate.angle = 90;
	andGate.params = { { "INPUT_BITS", "2" } };
	pg.gates.push_back(andGate);

	WireInstance wire;
	wire.ids = { "2001" };
	WireSegment s0;
	s0.id = "0";
	s0.vertical = false;
	s0.begin = { -5, 0 };
	s0.end = { 3, 0 };
	s0.connects = { { "1001", "OUT_0" } };
	s0.intersections = { { 3, "1" } }; // meets segment 1 at x=3
	WireSegment s1;
	s1.id = "1";
	s1.vertical = true;
	s1.begin = { 3, 0 };
	s1.end = { 3, 1 };
	s1.connects = { { "1002", "IN_0" } };
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

TEST_CASE("Malformed numbers report their context, not std::stod's") {
	const std::string doc =
	    "(cedarlogic (version 3) (generator \"t\")"
	    " (page 0 (gate \"AA_AND2\" (uuid \"1\") (at foo 0) (angle 0))))";
	CHECK_THROWS_WITH_AS(readCircuitFile(doc),
	                     doctest::Contains("malformed number \"foo\" in (at ...)"),
	                     std::runtime_error);
}

TEST_CASE("Trailing junk on a number is an error, not a prefix") {
	const std::string doc =
	    "(cedarlogic (version 3) (generator \"t\")"
	    " (page 0 (gate \"AA_AND2\" (uuid \"1\") (at 3abc 0) (angle 0))))";
	CHECK_THROWS_AS(readCircuitFile(doc), std::runtime_error);
}

TEST_CASE("Nesting is capped so a hostile file cannot overflow the stack") {
	// Far past the 5 levels a well-formed v3 document reaches. Without the cap
	// this recurses once per paren and dies on the stack instead of throwing.
	std::string deep = "(cedarlogic";
	deep.append(100000, '(');
	CHECK_THROWS_WITH_AS(parseSexpr(deep), doctest::Contains("nesting too deep"),
	                     std::runtime_error);
}

TEST_CASE("A real document stays well inside the nesting cap") {
	CircuitFile cf;
	cf.generator = "t";
	Page pg;
	GateInstance g;
	g.uuid = "1";
	g.libName = "AA_AND2";
	pg.gates.push_back(g);
	WireInstance w;
	w.ids = { "10" };
	WireSegment s;
	s.id = "0";
	s.connects.push_back({ "1", "OUT" });
	s.intersections.push_back({ 1.5, "1" });
	w.segments.push_back(s);
	pg.wires.push_back(w);
	cf.pages.push_back(pg);

	// Deepest v3 nesting is (cedarlogic (page (wire (seg (pts ...))))) = 5.
	const std::string text = writeCircuitFile(cf);
	int depth = 0, worst = 0;
	bool inString = false;
	for (size_t i = 0; i < text.size(); i++) {
		const char c = text[i];
		if (inString) {
			if (c == '\\') i++;
			else if (c == '"') inString = false;
		} else if (c == '"') inString = true;
		else if (c == '(') worst = std::max(worst, ++depth);
		else if (c == ')') depth--;
	}
	CHECK(worst == 5);
}

TEST_CASE("A leading UTF-8 byte order mark does not hide the format") {
	const std::string doc =
	    "(cedarlogic (version 3) (generator \"t\")"
	    " (page 0 (gate \"AA_AND2\" (uuid \"1\") (at 0 0) (angle 0))))";
	CHECK(detectFormat("\xEF\xBB\xBF" + doc) == SourceFormat::SexprV3);
	LoadResult r = loadCircuit("\xEF\xBB\xBF" + doc);
	REQUIRE(r.file.pages.size() == 1);
	CHECK(r.file.pages[0].gates.size() == 1);
}

TEST_CASE("Content after the document is an error, not something to ignore") {
	const std::string doc =
	    "(cedarlogic (version 3) (generator \"t\") (page 0))";
	CHECK_NOTHROW(readCircuitFile(doc));
	CHECK_NOTHROW(readCircuitFile(doc + "  \n\t "));   // trailing space is fine
	CHECK_THROWS_WITH_AS(readCircuitFile(doc + doc), doctest::Contains("trailing content"),
	                     std::runtime_error);
	CHECK_THROWS_AS(readCircuitFile(doc + " garbage"), std::runtime_error);
}

TEST_CASE("A segment orientation other than h or v is an error") {
	auto doc = [](const char *orient) {
		return std::string("(cedarlogic (version 3) (generator \"t\") (page 0 (wire (ids \"1\")"
		                   " (seg \"0\" ") + orient + " (pts 0 0 1 0)))))";
	};
	CHECK_NOTHROW(readCircuitFile(doc("h")));
	CHECK_NOTHROW(readCircuitFile(doc("v")));
	CHECK_THROWS_WITH_AS(readCircuitFile(doc("V")), doctest::Contains("is not h or v"),
	                     std::runtime_error);
	CHECK_THROWS_AS(readCircuitFile(doc("sideways")), std::runtime_error);
}

TEST_CASE("An id that is not a decimal integer is an error") {
	// Two such ids both convert to 0 downstream, so one object silently replaces
	// the other. Refusing the document is the only way the user hears about it.
	CHECK_THROWS_WITH_AS(
	    readCircuitFile("(cedarlogic (version 3) (generator \"t\") (page 0"
	                    " (gate \"AA_AND2\" (uuid \"alpha\") (at 0 0) (angle 0))))"),
	    doctest::Contains("not a decimal integer"), std::runtime_error);
	CHECK_THROWS_AS(
	    readCircuitFile("(cedarlogic (version 3) (generator \"t\") (page 0"
	                    " (wire (ids \"\") (seg \"0\" h (pts 0 0 1 0)))))"),
	    std::runtime_error);
	// A segment label may be absent. Legacy documents leave it out.
	CHECK_NOTHROW(loadCircuit("<circuit><page 0><wire><ID>5</ID><shape>"
	                          "<hsegment><points>0,0,1,0</points></hsegment>"
	                          "</shape></wire></page 0></circuit>"));
}
