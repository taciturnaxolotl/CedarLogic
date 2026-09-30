// Regression tests for the segment-map tidying that crashed CedarLogic when a
// gate was deleted (issue #110). Each case is a segment shape the old search
// could not survive; all three come from the same nine lines.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "wire/SegmentMap.h"

#include <stdexcept>

using cl::wire::rehomeConnections;
using cl::wire::SegmentMap;

namespace {

wireSegment zeroLength(long id) {
	return wireSegment(GLPoint2f(0, 0), GLPoint2f(0, 0), false, id);
}

wireSegment withLength(long id) {
	return wireSegment(GLPoint2f(0, 0), GLPoint2f(5, 0), false, id);
}

wireConnection conn(unsigned long gid, const char *hotspot) {
	wireConnection c;
	c.gid = gid;
	c.connection = hotspot;
	return c;
}

}  // namespace

TEST_CASE("connections move onto a neighbour that has length") {
	SegmentMap segs;
	segs.put(zeroLength(0));
	segs.at(0).connections.push_back(conn(7, "OUT"));
	segs.at(0).intersects[0.0f].push_back(1);
	segs.put(withLength(1));

	REQUIRE(rehomeConnections(segs, 0));
	CHECK(segs.at(1).connections.size() == 1);
	CHECK(segs.at(1).connections[0].gid == 7);
}

TEST_CASE("a segment with no intersections at all is not dereferenced") {
	// The old code took intersects.begin() without comparing it to end(), so
	// this read the map's sentinel node. AddressSanitizer called it a
	// heap-buffer-overflow.
	SegmentMap segs;
	segs.put(zeroLength(0));
	segs.at(0).connections.push_back(conn(7, "OUT"));
	segs.put(withLength(1));

	CHECK_FALSE(rehomeConnections(segs, 0));
	CHECK(segs.at(0).connections.size() == 1);  // kept, not dropped
}

TEST_CASE("two zero-length segments pointing at each other terminate") {
	// The old hop always took element [0], so these passed the search back and
	// forth forever.
	SegmentMap segs;
	segs.put(zeroLength(0));
	segs.at(0).connections.push_back(conn(7, "OUT"));
	segs.at(0).intersects[0.0f].push_back(1);
	segs.put(zeroLength(1));
	segs.at(1).intersects[0.0f].push_back(0);
	segs.put(withLength(2));  // has length, but unreachable from 0

	CHECK_FALSE(rehomeConnections(segs, 0));
}

TEST_CASE("an intersection naming a segment that is gone is ignored") {
	// The old lookup used operator[], which inserts, so a stale id grew a blank
	// segment and the search then followed it.
	SegmentMap segs;
	segs.put(zeroLength(0));
	segs.at(0).connections.push_back(conn(7, "OUT"));
	segs.at(0).intersects[0.0f].push_back(77);  // no such segment
	segs.put(withLength(1));

	CHECK_FALSE(rehomeConnections(segs, 0));
	CHECK(segs.size() == 2);  // nothing invented
}

TEST_CASE("the search crosses a zero-length segment to reach one with length") {
	SegmentMap segs;
	segs.put(zeroLength(0));
	segs.at(0).connections.push_back(conn(7, "OUT"));
	segs.at(0).intersects[0.0f].push_back(1);
	segs.put(zeroLength(1));
	segs.at(1).intersects[0.0f].push_back(2);
	segs.put(withLength(2));

	REQUIRE(rehomeConnections(segs, 0));
	CHECK(segs.at(2).connections.size() == 1);
}

TEST_CASE("a segment with nothing attached needs no host") {
	SegmentMap segs;
	segs.put(zeroLength(0));
	CHECK(rehomeConnections(segs, 0));
}

TEST_CASE("asking for a segment the wire does not have is an error, not a new one") {
	SegmentMap segs;
	segs.put(withLength(1));
	CHECK_THROWS_AS(segs.at(99), std::out_of_range);
	CHECK(segs.size() == 1);
}

TEST_CASE("a segment is keyed by the id it carries") {
	SegmentMap segs;
	segs.put(withLength(7));
	CHECK(segs.at(7).id == 7);
	CHECK(segs.find(7) != NULL);
	CHECK(segs.find(8) == NULL);
}

TEST_CASE("a default-constructed segment starts at the origin, horizontal, id 0") {
	// map::operator[] default-constructs on a miss, and these members used to
	// be left indeterminate.
	wireSegment s;
	CHECK(s.id == 0);
	CHECK(s.verticalSeg == false);
	CHECK(s.begin == GLPoint2f(0, 0));
	CHECK(s.end == GLPoint2f(0, 0));
}

// A gate remembers one wire per pin, so when a wire is displaced from a pin the
// gate can no longer name it and the old prune walked straight past it. These
// cover asking the segments directly instead (issue #127).

TEST_CASE("dropping a gate takes every segment that names it") {
	SegmentMap segs;
	segs.put(withLength(0));
	segs.at(0).connections.push_back(conn(7, "OUT"));
	segs.at(0).connections.push_back(conn(9, "IN"));
	segs.put(withLength(1));
	segs.at(1).connections.push_back(conn(7, "IN_1"));

	CHECK(cl::wire::dropGateConnections(segs, 7));
	CHECK(segs.at(0).connections.size() == 1);
	CHECK(segs.at(0).connections[0].gid == 9);
	CHECK(segs.at(1).connections.empty());
}

TEST_CASE("dropping a gate takes a pin listed twice on one segment") {
	// The coincident DATA_IN/DATA_OUT pins on the RAM gates connect as a pair,
	// and a pasted block replays the pair, so the same pin could be listed
	// twice. Removing one copy per call left the other behind for good.
	SegmentMap segs;
	segs.put(withLength(0));
	segs.at(0).connections.push_back(conn(7, "DATA_IN_0"));
	segs.at(0).connections.push_back(conn(7, "DATA_IN_0"));
	segs.at(0).connections.push_back(conn(8, "OUT"));

	CHECK(cl::wire::dropGateConnections(segs, 7));
	REQUIRE(segs.at(0).connections.size() == 1);
	CHECK(segs.at(0).connections[0].gid == 8);
}

TEST_CASE("dropping one pin leaves the gate's other pins alone") {
	SegmentMap segs;
	segs.put(withLength(4));
	segs.at(4).connections.push_back(conn(7, "DATA_IN_0"));
	segs.at(4).connections.push_back(conn(7, "DATA_OUT_0"));
	segs.at(4).connections.push_back(conn(7, "DATA_IN_0"));

	// The segment it came from, which is what trimming the tree needs.
	CHECK(cl::wire::dropConnection(segs, 7, "DATA_IN_0") == 4);
	REQUIRE(segs.at(4).connections.size() == 1);
	CHECK(segs.at(4).connections[0].connection == "DATA_OUT_0");
}

TEST_CASE("a connection taken from segment zero is not mistaken for none") {
	// Segment 0 is a real id, so "no segment held it" has to be a value no
	// segment can have. The old code used 0 for both and trimmed from whatever
	// happened to be there.
	SegmentMap segs;
	segs.put(withLength(0));
	segs.at(0).connections.push_back(conn(7, "OUT"));

	CHECK(cl::wire::dropConnection(segs, 7, "OUT") == 0);
	CHECK(cl::wire::dropConnection(segs, 7, "OUT") == -1);
}

TEST_CASE("dropping a gate that is not there changes nothing") {
	SegmentMap segs;
	segs.put(withLength(0));
	segs.at(0).connections.push_back(conn(7, "OUT"));

	CHECK_FALSE(cl::wire::dropGateConnections(segs, 42));
	CHECK(cl::wire::dropConnection(segs, 7, "IN") == -1);
	CHECK(segs.at(0).connections.size() == 1);
}
