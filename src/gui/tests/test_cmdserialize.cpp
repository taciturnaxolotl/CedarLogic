// Headless tests for the typed command serialization module. Two guarantees:
//   1. Golden format -- emit() produces the exact byte string the command
//      classes historically wrote by hand, so delegating to it changes nothing
//      on the clipboard/undo wire.
//   2. Round trip -- parse(emit(x)) == x for values that serialize exactly.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "cmdSerialize.h"

using namespace cmdser;

TEST_CASE("emit matches the historical hand-written formats (golden)") {
	CHECK(emit(CreateGate{5, "AND", 1.5f, -2.0f}) == "creategate 5 AND 1.5 -2");
	CHECK(emit(CreateGate{0, "BE_DECODER_3x8", 0.0f, 0.0f}) ==
	      "creategate 0 BE_DECODER_3x8 0 0");
	CHECK(emit(ConnectWire{3, 7, "IN_0"}) == "connectwire 3 7 IN_0");
	CHECK(emit(MoveGate{2, 1.0f, 2.0f, 3.0f, 4.0f}) == "movegate 2 1 2 3 4");
	CHECK(emit(MoveGate{9, -1.5f, 0.0f, 2.25f, -4.0f}) ==
	      "movegate 9 -1.5 0 2.25 -4");
	CHECK(emit(DisconnectWire{5, 9, "IN_0"}) == "disconnectwire 5 9 IN_0");
	CHECK(emit(CreateWire{{3, 4}, {3, 10, "OUT_0"}, {4, 11, "IN_1"}}) ==
	      "createwire 3 4 connectwire 3 10 OUT_0 connectwire 4 11 IN_1");
}

TEST_CASE("parse consumes the leading keyword and fields") {
	CreateGate cg;
	REQUIRE(parse("creategate 12 OR 3.5 -1", cg));
	CHECK(cg.gid == 12);
	CHECK(cg.gateType == "OR");
	CHECK(cg.x == doctest::Approx(3.5));
	CHECK(cg.y == doctest::Approx(-1.0));

	ConnectWire cw;
	REQUIRE(parse("connectwire 100 200 OUT_3", cw));
	CHECK(cw.wireId == 100);
	CHECK(cw.gateId == 200);
	CHECK(cw.hotspot == "OUT_3");
}

TEST_CASE("keyword returns the leading token the dispatcher matches on") {
	CHECK(keyword("creategate 5 AND 1 2") == "creategate");
	CHECK(keyword("connectwire 3 7 IN_0") == "connectwire");
	CHECK(keyword("movegate 2 1 2 3 4") == "movegate");
	CHECK(keyword("") == "");
}

TEST_CASE("parse rejects a mismatched keyword") {
	CreateGate cg;
	CHECK_FALSE(parse("connectwire 1 2 IN_0", cg));
	ConnectWire cw;
	CHECK_FALSE(parse("movegate 1 2 3 4 5", cw));
}

TEST_CASE("round trip is stable for exactly-representable values") {
	CreateGate cg{42, "XOR", 2.5f, -8.0f};
	CreateGate cg2;
	REQUIRE(parse(emit(cg), cg2));
	CHECK(cg2.gid == cg.gid);
	CHECK(cg2.gateType == cg.gateType);
	CHECK(cg2.x == doctest::Approx(cg.x));
	CHECK(cg2.y == doctest::Approx(cg.y));

	ConnectWire cw{7, 13, "IN_2"};
	ConnectWire cw2;
	REQUIRE(parse(emit(cw), cw2));
	CHECK(cw2.wireId == cw.wireId);
	CHECK(cw2.gateId == cw.gateId);
	CHECK(cw2.hotspot == cw.hotspot);
}

TEST_CASE("createwire round-trips its id list and both connections") {
	// The id list is variable length and ends where the first connectwire begins.
	CreateWire cw{{7, 8, 9}, {7, 20, "OUT_0"}, {9, 21, "IN_3"}};
	CreateWire out;
	REQUIRE(parse(emit(cw), out));
	CHECK(out.wireIds == cw.wireIds);
	CHECK(out.conn1.wireId == cw.conn1.wireId);
	CHECK(out.conn1.gateId == cw.conn1.gateId);
	CHECK(out.conn1.hotspot == cw.conn1.hotspot);
	CHECK(out.conn2.wireId == cw.conn2.wireId);
	CHECK(out.conn2.gateId == cw.conn2.gateId);
	CHECK(out.conn2.hotspot == cw.conn2.hotspot);
}
