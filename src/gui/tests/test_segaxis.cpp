// Which coordinate runs along a segment and which runs across it. Small on its
// own, but every collapsed branch in the wire code rests on it, and two of its
// behaviours are the kind a rewrite gets wrong silently.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "wireSegment.h"

#include <cmath>

namespace {

wireSegment upright() { return wireSegment(GLPoint2f(0, 0), GLPoint2f(0, 5), true, 0); }
wireSegment flat()    { return wireSegment(GLPoint2f(0, 0), GLPoint2f(5, 0), false, 0); }

klsBBox boxSpanning(float x0, float y0, float x1, float y1) {
	klsBBox b;
	b.addPoint(GLPoint2f(x0, y0));
	b.addPoint(GLPoint2f(x1, y1));
	return b;
}

}  // namespace

TEST_CASE("along and across pick the coordinate the orientation asks for") {
	SegAxis v = upright().axis();
	SegAxis h = flat().axis();
	GLPoint2f p(3, 7);

	CHECK(v.along(p) == 7);
	CHECK(v.across(p) == 3);
	CHECK(h.along(p) == 3);
	CHECK(h.across(p) == 7);
}

TEST_CASE("along and across can be written through") {
	// They hand back a reference, not a copy. Returning by value would compile
	// just as happily and silently discard every assignment.
	SegAxis v = upright().axis();
	GLPoint2f p(3, 7);

	v.along(p) = 11;
	CHECK(p.y == 11);
	CHECK(p.x == 3);

	v.across(p) = 13;
	CHECK(p.x == 13);
	CHECK(p.y == 11);
}

TEST_CASE("perp flips, and matches compares") {
	SegAxis v = upright().axis();

	CHECK(v.perp().vertical == false);
	CHECK(v.perp().perp().vertical == true);
	CHECK(v.matches(upright()));
	CHECK_FALSE(v.matches(flat()));
}

TEST_CASE("a segment knows its own axis") {
	CHECK(upright().axis().vertical == true);
	CHECK(flat().axis().vertical == false);
}

TEST_CASE("the mouse edge read is left for upright and top for flat") {
	// Not a mirror, and deliberately so: this is what the drags have always
	// read, and a drag asks on the axis it moves along, which is the
	// perpendicular of the segment being dragged. Every caller passes a point
	// box today, where the two edges agree, so a rewrite that "fixed" it would
	// change nothing visible and break the first caller that passes an extent.
	SegAxis v{ true };
	SegAxis h{ false };
	klsBBox b = boxSpanning(2, 3, 8, 9);

	CHECK(v.acrossEdge(b) == 2);   // left
	CHECK(h.acrossEdge(b) == 9);   // top

	// On a point box, which is all a drag ever gets, they coincide.
	klsBBox point = boxSpanning(5, 5, 5, 5);
	CHECK(h.acrossEdge(point) == 5);
	CHECK(v.acrossEdge(point) == 5);
}

TEST_CASE("a quarter turn is exact, so a turned gate's pins stay on the grid") {
	// The old 0.0174533 for pi/180 left a pin on a gate turned 180 degrees 3e-6 off its line.
	CHECK(sinCosDegrees(0).sin == 0);    CHECK(sinCosDegrees(0).cos == 1);
	CHECK(sinCosDegrees(90).sin == 1);   CHECK(sinCosDegrees(90).cos == 0);
	CHECK(sinCosDegrees(180).sin == 0);  CHECK(sinCosDegrees(180).cos == -1);
	CHECK(sinCosDegrees(270).sin == -1); CHECK(sinCosDegrees(270).cos == 0);
	CHECK(sinCosDegrees(-90).sin == -1); CHECK(sinCosDegrees(-90).cos == 0);
	CHECK(sinCosDegrees(450).sin == 1);  CHECK(sinCosDegrees(450).cos == 0);
	CHECK(sinCosDegrees(45).sin == doctest::Approx(std::sqrt(0.5)));
	CHECK(sinCosDegrees(45).cos == doctest::Approx(std::sqrt(0.5)));
}

TEST_CASE("the shared tolerance treats a hair as the same and a step as different") {
	CHECK(nearly(5.0f, 5.0000029f));
	CHECK_FALSE(nearly(5.0f, 5.01f));
	CHECK(spansTouch(0, 5, 5.001f, 9));      // meet within tolerance
	CHECK(spansTouch(0, 10, 3, 4));          // one inside the other
	CHECK_FALSE(spansTouch(0, 5, 5.01f, 9)); // a real gap
	CHECK(spansTouch(5, 0, 9, 5.001f));      // ends given high first
}
