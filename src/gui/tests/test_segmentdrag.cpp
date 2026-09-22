// Dragging one segment of a wire and reattaching a connection after its gate
// moves, exercised from a shape and a table of hotspots.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "wire/SegmentDrag.h"

#include <map>
#include <string>
#include <vector>

using cl::wire::beginSegDrag;
using cl::wire::endSegDrag;
using cl::wire::updateConnectionPos;
using cl::wire::updateSegDrag;
using cl::wire::SegmentMap;
using cl::wire::WireShape;

namespace {

// The circuit, replaced: connections are looked up in a table.
class FakeHotspots : public cl::wire::Hotspots {
public:
	void place(unsigned long gid, const char *name, GLPoint2f at, bool vertical = false) {
		Entry e; e.at = at; e.vertical = vertical;
		table[key(gid, name)] = e;
	}
	GLPoint2f coordsOf(const wireConnection &c) const override {
		return table.at(key(c.gid, c.connection.c_str())).at;
	}
	bool isVertical(const wireConnection &c) const override {
		return table.at(key(c.gid, c.connection.c_str())).vertical;
	}
private:
	struct Entry { GLPoint2f at; bool vertical; };
	static std::string key(unsigned long gid, const char *name) {
		return std::to_string(gid) + ":" + name;
	}
	std::map<std::string, Entry> table;
};

wireConnection conn(unsigned long gid, const char *hotspot) {
	wireConnection c;
	c.gid = gid;
	c.connection = hotspot;
	return c;
}

// An L: an upright trunk at x=0 from y=0 to y=1, a flat run to a gate at
// (5,1), and a flat run back to a gate at (-5,0). The two flat runs each carry
// one connection and cross the trunk.
WireShape elbow() {
	WireShape shape;
	shape.segs.put(wireSegment(GLPoint2f(0, 0), GLPoint2f(0, 1), true, 0));
	shape.segs.put(wireSegment(GLPoint2f(0, 1), GLPoint2f(5, 1), false, 1));
	shape.segs.at(1).connections.push_back(conn(2, "IN"));
	shape.segs.put(wireSegment(GLPoint2f(-5, 0), GLPoint2f(0, 0), false, 2));
	shape.segs.at(2).connections.push_back(conn(1, "OUT"));
	shape.segs.at(0).intersects[0.0f].push_back(2);
	shape.segs.at(0).intersects[1.0f].push_back(1);
	shape.segs.at(1).intersects[0.0f].push_back(0);
	shape.segs.at(2).intersects[0.0f].push_back(0);
	shape.head = 0;
	shape.nextID = 3;
	for (auto &kv : shape.segs) kv.second.calcBBox();
	return shape;
}

FakeHotspots hotspotsForElbow() {
	FakeHotspots hs;
	hs.place(1, "OUT", GLPoint2f(-5, 0));
	hs.place(2, "IN", GLPoint2f(5, 1));
	return hs;
}

// A point box, which is what the canvas hands a drag.
klsBBox at(float x, float y) {
	klsBBox b;
	b.addPoint(GLPoint2f(x, y));
	return b;
}

}  // namespace

TEST_CASE("dragging an upright segment moves it across and stretches its neighbours") {
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();

	REQUIRE(beginSegDrag(0, shape, hs, at(0, 0.5f)));
	CHECK(shape.dragging == 0);

	updateSegDrag(shape, hs, at(2, 0.5f));

	// The trunk slid two to the right.
	CHECK(shape.segs.at(0).begin.x == 2);
	CHECK(shape.segs.at(0).end.x == 2);
	// Both neighbours grew to stay attached, each to its own connection.
	CHECK(shape.segs.at(1).begin.x == 2);
	CHECK(shape.segs.at(1).end.x == 5);
	CHECK(shape.segs.at(2).begin.x == -5);
	CHECK(shape.segs.at(2).end.x == 2);
}

TEST_CASE("dragging a flat segment moves it up and stretches its neighbours") {
	// The other half of the same move. The drag probe only ever picked the
	// longest segment, which is always flat, so the upright half above went
	// untried by every capture.
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();

	REQUIRE(beginSegDrag(1, shape, hs, at(2.5f, 1)));
	updateSegDrag(shape, hs, at(2.5f, 4));

	CHECK(shape.segs.at(1).begin.y == 4);
	CHECK(shape.segs.at(1).end.y == 4);
	// The trunk now has to span from the other connection up to the moved one.
	CHECK(shape.segs.at(0).begin.y == 0);
	CHECK(shape.segs.at(0).end.y == 4);
	// And it has to have slid across to stay under the segment it joins.
	CHECK(shape.segs.at(0).begin.x == 0);
	CHECK(shape.segs.at(0).end.x == 0);
}

TEST_CASE("starting a drag lifts the connections off onto their own stubs") {
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();
	size_t before = shape.segs.size();

	// Segment 1 is the flat run holding the IN connection.
	REQUIRE(beginSegDrag(1, shape, hs, at(2.5f, 1)));

	// The dragged segment gave up its connection and the tree grew to hold it;
	// nothing was lost.
	CHECK(shape.segs.at(1).connections.empty());
	CHECK(shape.segs.size() == before + 1);
	size_t total = 0;
	for (auto &kv : shape.segs) total += kv.second.connections.size();
	CHECK(total == 2);
}

TEST_CASE("starting a drag on a segment with no connections grows nothing") {
	// The trunk crosses the other two but holds nothing itself, so there is
	// nothing to lift off and no stub to make.
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();
	size_t before = shape.segs.size();

	REQUIRE(beginSegDrag(0, shape, hs, at(0, 0.5f)));

	CHECK(shape.segs.at(0).connections.empty());
	CHECK(shape.segs.size() == before);
}

TEST_CASE("a drag snapshots the shape it started from") {
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();

	REQUIRE(beginSegDrag(0, shape, hs, at(0, 0.5f)));
	updateSegDrag(shape, hs, at(2, 0.5f));

	REQUIRE(shape.before.has(0));
	CHECK(shape.before.at(0).begin.x == 0);   // where the trunk was
	CHECK(shape.before.size() == 3);
}

TEST_CASE("ending a drag clears it, and a later move does nothing") {
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();

	REQUIRE(beginSegDrag(0, shape, hs, at(0, 0.5f)));
	endSegDrag(shape);
	CHECK(shape.dragging == -1);

	// Nothing is being dragged, so this must leave the shape alone.
	float x = shape.segs.at(0).begin.x;
	updateSegDrag(shape, hs, at(9, 9));
	CHECK(shape.segs.at(0).begin.x == x);
}

TEST_CASE("dragging a segment the wire does not have is refused") {
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();

	CHECK_FALSE(beginSegDrag(99, shape, hs, at(0, 0.5f)));
	CHECK(shape.dragging == -1);
	CHECK(shape.before.empty());
}

TEST_CASE("a moved gate takes its connection with it") {
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();
	hs.place(2, "IN", GLPoint2f(8, 3));   // the gate has moved

	updateConnectionPos(2, "IN", shape, hs);

	// The connection now sits where the gate is, on a stub of the orientation
	// its hotspot calls for. IN is flat, so the stub is flat and pinned to the
	// gate's y. Naming the orientation is what catches the two branches being
	// swapped; accepting either would pass just as happily.
	bool reaches = false;
	for (auto &kv : shape.segs)
		for (const wireConnection &c : kv.second.connections)
			if (c.gid == 2) {
				reaches = true;
				const wireSegment &s = kv.second;
				CHECK_FALSE(s.isVertical());
				CHECK(s.begin.y == 3);
				CHECK(s.end.y == 3);
			}
	CHECK(reaches);
	// The other connection did not go anywhere.
	bool otherKept = false;
	for (auto &kv : shape.segs)
		for (const wireConnection &c : kv.second.connections)
			if (c.gid == 1) otherKept = true;
	CHECK(otherKept);
}

TEST_CASE("reattaching a connection the wire does not hold changes nothing") {
	WireShape shape = elbow();
	FakeHotspots hs = hotspotsForElbow();
	size_t before = shape.segs.size();

	updateConnectionPos(9, "NOPE", shape, hs);

	CHECK(shape.segs.size() == before);
}
