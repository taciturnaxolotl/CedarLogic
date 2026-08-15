#include <doctest/doctest.h>

#include "legacy_cdl.hpp"

using namespace cl;

// A faithful slice of a v1 .cdl: a bare <circuit>, pages named "page N", gates
// carrying <gparam>/<lparam> plus redundant <input>/<output> pin blocks (which
// the reader ignores — connectivity comes from the wire side), and a wire whose
// <shape> branches across h/v segments each carrying points and a connection.
static const char *kV1 = R"CDL(<circuit>
<page 0>
<PageViewport>-113.179,16.25,-24.4457,-30.3</PageViewport>
<gate>
<ID>0</ID>
<type>AA_TOGGLE</type>
<position>-50,10</position>
<output>
<ID>OUT_0</ID>
<wire>100</wire>
</output>
<gparam>angle 0.0</gparam>
<lparam>OUTPUT_NUM 0</lparam>
</gate>
<gate>
<ID>1</ID>
<type>AA_AND2</type>
<position>20,-4</position>
<input>
<ID>IN_0</ID>
<wire>100</wire>
</input>
<gparam>angle 90.0</gparam>
<lparam>INPUT_BITS 2</lparam>
</gate>
<wire>
<ID>100 </ID>
<shape>
<hsegment>
<ID>0</ID>
<points>-45,10,10,10</points>
<connection>
<GID>0</GID>
<name>OUT_0</name>
</connection>
<intersection>10 1</intersection>
</hsegment>
<vsegment>
<ID>1</ID>
<points>10,10,10,-4</points>
<connection>
<GID>1</GID>
<name>IN_0</name>
</connection>
<intersection>10 0</intersection>
</vsegment>
</shape>
</wire>
</page 0>
<page 1>
<gate>
<ID>2</ID>
<type>AA_LABEL</type>
<position>0,0</position>
<gparam>angle 0.0</gparam>
</gate>
</page 1>
</circuit>
)CDL";

// v2 wraps the same structure in a decoy <circuit>, a <throw_away> sentinel, and
// a <version> tag; the real circuit is the last one.
static const char *kV2 = R"CDL(<circuit>
<garbage>decoy</garbage>
</circuit>
<throw_away>
</throw_away>
<version>2.3</version>
<circuit>
<page 0>
<gate>
<ID>0</ID>
<type>AA_AND2</type>
<position>0,0</position>
<gparam>angle 0.0</gparam>
<lparam>INPUT_BITS 2</lparam>
</gate>
</page 0>
</circuit>
)CDL";

TEST_CASE("detectFormat distinguishes v1, v2, v3, and junk") {
	CHECK(detectFormat(kV1) == SourceFormat::XmlV1);
	CHECK(detectFormat(kV2) == SourceFormat::XmlV2);
	CHECK(detectFormat("  (cedarlogic (version 3))") == SourceFormat::SexprV3);
	CHECK(detectFormat("(something-else)") == SourceFormat::Unknown);
	CHECK(detectFormat("") == SourceFormat::Unknown);
}

TEST_CASE("readLegacyCdl maps a v1 circuit into the model") {
	CircuitFile cf = readLegacyCdl(kV1);
	REQUIRE(cf.pages.size() == 2);

	const Page &p0 = cf.pages[0];
	CHECK(p0.index == 0);
	REQUIRE(p0.gates.size() == 2);

	const GateInstance &toggle = p0.gates[0];
	CHECK(toggle.uuid == "0");
	CHECK(toggle.libName == "AA_TOGGLE");
	CHECK(toggle.at == XY{ -50, 10 });
	CHECK(toggle.angle == 0.0);
	REQUIRE(toggle.params.size() == 1);         // angle is lifted out, not a param
	CHECK(toggle.params[0].name == "OUTPUT_NUM");
	CHECK(toggle.params[0].value == "0");
	CHECK(toggle.params[0].gui == false);       // <lparam> -> logic

	const GateInstance &andGate = p0.gates[1];
	CHECK(andGate.angle == 90.0);
	CHECK(andGate.at == XY{ 20, -4 });

	REQUIRE(p0.wires.size() == 1);
	const WireInstance &w = p0.wires[0];
	REQUIRE(w.ids.size() == 1);
	CHECK(w.ids[0] == "100");                      // trailing space trimmed
	REQUIRE(w.segments.size() == 2);              // one h + one v segment
	CHECK(w.segments[0].id == "0");
	CHECK(w.segments[0].vertical == false);
	CHECK(w.segments[0].begin == XY{ -45, 10 });
	CHECK(w.segments[0].end == XY{ 10, 10 });
	REQUIRE(w.segments[0].connects.size() == 1);
	CHECK(w.segments[0].connects[0] == WireConn{ "0", "OUT_0" });
	REQUIRE(w.segments[0].intersections.size() == 1);
	CHECK(w.segments[0].intersections[0] == Intersection{ 10, "1" }); // meets seg 1 at x=10
	CHECK(w.segments[1].id == "1");
	CHECK(w.segments[1].vertical == true);
	CHECK(w.segments[1].end == XY{ 10, -4 });
	REQUIRE(w.segments[1].connects.size() == 1);
	CHECK(w.segments[1].connects[0] == WireConn{ "1", "IN_0" });
	REQUIRE(w.segments[1].intersections.size() == 1);
	CHECK(w.segments[1].intersections[0] == Intersection{ 10, "0" });

	CHECK(cf.pages[1].index == 1);
}

TEST_CASE("readLegacyCdl skips the v2 decoy and takes the real circuit") {
	CircuitFile cf = readLegacyCdl(kV2);
	REQUIRE(cf.pages.size() == 1);
	REQUIRE(cf.pages[0].gates.size() == 1);
	CHECK(cf.pages[0].gates[0].libName == "AA_AND2");
	CHECK(cf.generator == "imported from CedarLogic 2.3");
	CHECK(cf.formatVersion == 3);
}

TEST_CASE("A '<' escaped as the legacy BEL byte is restored in a value") {
	// The legacy writer stored '<' inside values as 0x07 to survive its pseudo-XML.
	const std::string src =
	    "<circuit><page 0><gate><ID>0</ID><type>AA_LABEL</type>"
	    "<position>0,0</position>"
	    "<gparam>LABEL_TEXT a\x07" "b</gparam>"   // "a<b", split to stop \x eating 'b'
	    "</gate></page 0></circuit>";
	CircuitFile cf = readLegacyCdl(src);
	REQUIRE(cf.pages.size() == 1);
	REQUIRE(cf.pages[0].gates.size() == 1);
	const GateInstance &g = cf.pages[0].gates[0];
	REQUIRE(g.params.size() == 1);
	CHECK(g.params[0].name == "LABEL_TEXT");
	CHECK(g.params[0].value == "a<b");
	CHECK(g.params[0].gui == true);               // <gparam> -> GUI
}

TEST_CASE("readLegacyCdl rejects input with no circuit") {
	CHECK_THROWS(readLegacyCdl("<notacircuit></notacircuit>"));
}

TEST_CASE("readLegacyCdl rejects an unterminated document") {
	CHECK_THROWS(readLegacyCdl("<circuit><page 0><gate><ID>0</ID>"));
}
