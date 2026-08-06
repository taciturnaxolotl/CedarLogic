#include <doctest/doctest.h>

#include "circuit_file_io.hpp"
#include "migrate.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace cl;

#ifndef FIXTURES_DIR
#define FIXTURES_DIR "."
#endif

static std::string slurp(const std::string &name) {
	std::ifstream f(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
	REQUIRE_MESSAGE(f.good(), "missing fixture: " << name);
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

namespace {
struct Expect {
	const char *file;
	SourceFormat format;
	int pages, gates, wires;
};
} // namespace

// Real circuits saved by CedarLogic. Counts are what the reader currently
// extracts; they pin the parse against regressions. v1 files are bare
// <circuit>; the two large ones are v2 (decoy + throw_away + version wrapper).
static const Expect kFixtures[] = {
	{ "Broadcast.cdl", SourceFormat::XmlV1, 10, 51, 40 },
	{ "Lab5.cdl", SourceFormat::XmlV1, 10, 53, 40 },
	{ "Routing Backwards.cdl", SourceFormat::XmlV1, 10, 56, 43 },
	{ "jmSGTZM.cdl", SourceFormat::XmlV2, 3, 699, 462 },
	{ "lab6.cdl", SourceFormat::XmlV2, 3, 699, 468 },
};

TEST_CASE("Real .cdl files parse, count out, and round-trip through v3") {
	for (const Expect &e : kFixtures) {
		CAPTURE(e.file);
		std::string text = slurp(e.file);

		CHECK(detectFormat(text) == e.format);

		LoadResult r = loadCircuit(text);
		CHECK(r.source == e.format);
		CHECK(static_cast<int>(r.file.pages.size()) == e.pages);

		int gates = 0, wires = 0;
		for (const Page &pg : r.file.pages) {
			gates += static_cast<int>(pg.gates.size());
			wires += static_cast<int>(pg.wires.size());
		}
		CHECK(gates == e.gates);
		CHECK(wires == e.wires);

		// Every gate keeps a library name and every wire both a route and at least
		// one connection; no field got dropped on the floor during the walk.
		for (const Page &pg : r.file.pages) {
			for (const GateInstance &g : pg.gates) CHECK(!g.libName.empty());
			for (const WireInstance &w : pg.wires) {
				CHECK(!w.route.empty());
				CHECK(!w.connects.empty());
			}
		}

		// The v3 serializer is stable: model -> text -> model is a fixed point.
		std::string v3 = writeCircuitFile(r.file);
		CircuitFile back = readCircuitFile(v3);
		CHECK(back == r.file);
		CHECK(writeCircuitFile(back) == v3);
	}
}
