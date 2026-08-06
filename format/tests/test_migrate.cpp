#include <doctest/doctest.h>

#include "migrate.hpp"

using namespace cl;

static GateInstance gate(const std::string &uuid, const std::string &lib,
                         std::vector<Param> params = {}) {
	GateInstance g;
	g.uuid = uuid;
	g.libName = lib;
	g.params = std::move(params);
	return g;
}

static const MigrationNotice *noticeFor(const std::vector<MigrationNotice> &ns,
                                        const std::string &uuid) {
	for (const MigrationNotice &n : ns)
		if (n.gateUuid == uuid) return &n;
	return nullptr;
}

TEST_CASE("Deprecated gates are renamed and reported; the silent alias stays quiet") {
	CircuitFile cf;
	Page pg;
	pg.gates = { gate("a", "AA_DFF"), gate("b", "BA_JKFF"), gate("c", "BA_JKFF_NT"),
	             gate("d", "AM_RAM_16x16_Single_Port"), gate("e", "AA_AND2") };
	cf.pages.push_back(pg);

	std::vector<MigrationNotice> ns = migrate(cf);

	// libNames rewritten in place
	CHECK(cf.pages[0].gates[0].libName == "AE_DFF_LOW");
	CHECK(cf.pages[0].gates[1].libName == "BE_JKFF_LOW");
	CHECK(cf.pages[0].gates[2].libName == "BE_JKFF_LOW_NT");
	CHECK(cf.pages[0].gates[3].libName == "AM_RAM_16x16");
	CHECK(cf.pages[0].gates[4].libName == "AA_AND2"); // untouched

	CHECK(ns.size() == 4); // the AND gate produced nothing

	// The three flip-flops are behavior-changing warnings; all auto-fixed.
	for (const char *uuid : { "a", "b", "c" }) {
		const MigrationNotice *n = noticeFor(ns, uuid);
		REQUIRE(n != nullptr);
		CHECK(n->severity == Severity::Warning);
		CHECK(n->autoFixed == true);
	}
	// The RAM rename is a silent info alias.
	const MigrationNotice *ram = noticeFor(ns, "d");
	REQUIRE(ram != nullptr);
	CHECK(ram->severity == Severity::Info);
	CHECK(ram->libName == "AM_RAM_16x16");
}

TEST_CASE("A 3-bit decoder with no wire on the doomed output is corrected silently") {
	// The core fixes the width regardless; the migration only speaks up on data
	// loss. Real circuits carry many such decoders, so the safe case must be quiet.
	CircuitFile cf;
	Page pg;
	pg.gates = { gate("dec", "BE_DECODER_3x8", { { "INPUT_BITS", "3", false } }) };
	WireInstance w;               // wired to a surviving output
	w.uuid = "1";
	w.connects = { { "dec", "OUT_0" } };
	pg.wires.push_back(w);
	cf.pages.push_back(pg);

	CHECK(migrate(cf).empty());
}

TEST_CASE("A 3-bit decoder with a wire on OUT_8 warns and is not auto-fixed") {
	CircuitFile cf;
	Page pg;
	pg.gates = { gate("dec", "BE_DECODER_3x8", { { "INPUT_BITS", "3", false } }) };
	WireInstance w;
	w.uuid = "1";
	w.connects = { { "dec", "OUT_8" } };   // this output vanishes after the fix
	pg.wires.push_back(w);
	cf.pages.push_back(pg);

	std::vector<MigrationNotice> ns = migrate(cf);
	REQUIRE(ns.size() == 1);
	CHECK(ns[0].severity == Severity::Warning);
	CHECK(ns[0].autoFixed == false);
	CHECK(ns[0].summary.find("OUT_8") != std::string::npos);
}

TEST_CASE("2x4 and 4x16 decoders are unaffected (buggy and fixed widths coincide)") {
	CircuitFile cf;
	Page pg;
	pg.gates = { gate("d2", "BA_DECODER_2x4", { { "INPUT_BITS", "2", false } }),
	             gate("d4", "BI_DECODER_4x16", { { "INPUT_BITS", "4", false } }) };
	cf.pages.push_back(pg);

	CHECK(migrate(cf).empty());
}

TEST_CASE("loadCircuit reads a legacy v1 file and returns migration notices") {
	const std::string v1 =
	    "<circuit><page 0>"
	    "<gate><ID>0</ID><type>AA_DFF</type><position>0,0</position>"
	    "<gparam>angle 0.0</gparam></gate>"
	    "<gate><ID>1</ID><type>BE_DECODER_3x8</type><position>10,0</position>"
	    "<lparam>INPUT_BITS 3</lparam></gate>"
	    "<wire><ID>5</ID><shape><hsegment><points>0,0,10,0</points>"
	    "<connection><GID>1</GID><name>OUT_8</name></connection>"
	    "</hsegment></shape></wire>"
	    "</page 0></circuit>";

	LoadResult r = loadCircuit(v1);
	CHECK(r.source == SourceFormat::XmlV1);
	CHECK(r.file.pages.size() == 1);
	CHECK(r.file.pages[0].gates[0].libName == "AE_DFF_LOW"); // migrated in place

	const MigrationNotice *dff = noticeFor(r.notices, "0");
	REQUIRE(dff != nullptr);
	CHECK(dff->severity == Severity::Warning);

	const MigrationNotice *dec = noticeFor(r.notices, "1");
	REQUIRE(dec != nullptr);
	CHECK(dec->severity == Severity::Warning); // OUT_8 is wired
	CHECK(dec->autoFixed == false);
}

TEST_CASE("loadCircuit passes a v3 document through without migrating") {
	// Minimal valid v3 doc with a decoder that would trip the legacy handler if run.
	const std::string v3 =
	    "(cedarlogic (version 3) (generator \"t\")"
	    " (page 0 (gate \"BE_DECODER_3x8\" (uuid \"x\") (at 0 0) (angle 0)"
	    " (lparam \"INPUT_BITS\" \"3\"))))";
	LoadResult r = loadCircuit(v3);
	CHECK(r.source == SourceFormat::SexprV3);
	CHECK(r.notices.empty());
}
