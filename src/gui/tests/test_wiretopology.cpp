// The segment tree's own rules, exercised with no gate, canvas or display
// behind them.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "wire/WireTopology.h"

using cl::wire::refreshIntersections;
using cl::wire::SegmentMap;

namespace {

wireSegment horizontal(long id, float y, float x0, float x1) {
	return wireSegment(GLPoint2f(x0, y), GLPoint2f(x1, y), false, id);
}

wireSegment vertical(long id, float x, float y0, float y1) {
	return wireSegment(GLPoint2f(x, y0), GLPoint2f(x, y1), true, id);
}

}  // namespace

TEST_CASE("a crossing is re-keyed to where the crossed segment moved to") {
	// A vertical segment keys its crossings by y, and the horizontal segment it
	// crosses has since slid up. The old key is stale until this runs.
	SegmentMap segs;
	segs.put(vertical(0, 0, 0, 10));
	segs.at(0).intersects[2.0f].push_back(1);
	segs.put(horizontal(1, 7, -5, 5));

	refreshIntersections(segs);

	CHECK(segs.at(0).intersects.count(2.0f) == 0);
	REQUIRE(segs.at(0).intersects.count(7.0f) == 1);
	CHECK(segs.at(0).intersects[7.0f][0] == 1);
}

TEST_CASE("a horizontal segment keys its crossings by x, not y") {
	SegmentMap segs;
	segs.put(horizontal(0, 0, -5, 5));
	segs.at(0).intersects[99.0f].push_back(1);
	segs.put(vertical(1, 3, -2, 2));

	refreshIntersections(segs);

	REQUIRE(segs.at(0).intersects.count(3.0f) == 1);
	CHECK(segs.at(0).intersects[3.0f][0] == 1);
}

TEST_CASE("a crossing naming a segment that is gone is dropped") {
	// Looking the id up used to insert, which grew a blank segment at the
	// origin and then keyed the crossing to it.
	SegmentMap segs;
	segs.put(vertical(0, 0, 0, 10));
	segs.at(0).intersects[2.0f].push_back(77);  // no such segment

	refreshIntersections(segs);

	CHECK(segs.at(0).intersects.empty());
	CHECK(segs.size() == 1);  // nothing invented
}

TEST_CASE("two segments crossing at the same place keep both ids") {
	SegmentMap segs;
	segs.put(vertical(0, 0, 0, 10));
	segs.at(0).intersects[1.0f].push_back(1);
	segs.at(0).intersects[2.0f].push_back(2);
	segs.put(horizontal(1, 4, -5, 5));
	segs.put(horizontal(2, 4, -1, 9));  // same y, so both land on one key

	refreshIntersections(segs);

	REQUIRE(segs.at(0).intersects.count(4.0f) == 1);
	CHECK(segs.at(0).intersects[4.0f].size() == 2);
}
