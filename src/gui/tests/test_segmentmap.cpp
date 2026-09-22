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
