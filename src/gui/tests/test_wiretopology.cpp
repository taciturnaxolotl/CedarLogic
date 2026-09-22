// The segment tree's own rules, exercised with no gate, canvas or display
// behind them.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "wire/WireTopology.h"

#include <map>
#include <string>
#include <vector>

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

// A stand-in for the circuit: connections are looked up in a table instead of
// resolved through a gate, which is the whole point of the interface.
namespace {

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

}  // namespace

TEST_CASE("two segments on the same line become one") {
	// No capture reaches this: a freshly routed wire never holds two segments
	// to join, so the whole joining body went unchecked.
	FakeHotspots hs;
	hs.place(1, "OUT", GLPoint2f(-5, 0));
	SegmentMap segs;
	segs.put(horizontal(0, 0, -5, 0));
	segs.at(0).connections.push_back(conn(1, "OUT"));
	segs.put(horizontal(1, 0, 0, 5));
	long head = 0, next = 2;

	cl::wire::mergeSegments(segs, hs, { conn(1, "OUT") }, head, next);

	CHECK(segs.size() == 1);
}

TEST_CASE("parallel segments in different channels are left alone") {
	FakeHotspots hs;
	hs.place(1, "OUT", GLPoint2f(-5, 0));
	hs.place(2, "IN", GLPoint2f(-5, 3));
	SegmentMap segs;
	segs.put(horizontal(0, 0, -5, 5));
	segs.at(0).connections.push_back(conn(1, "OUT"));
	segs.put(horizontal(1, 3, -5, 5));   // parallel, different y
	segs.at(1).connections.push_back(conn(2, "IN"));
	long head = 0, next = 2;

	cl::wire::mergeSegments(segs, hs, { conn(1, "OUT"), conn(2, "IN") }, head, next);

	CHECK(segs.size() == 2);
}

TEST_CASE("parallel upright segments in different channels are left alone") {
	// The same rule the other way up. Checking only one orientation lets the
	// other one's channel test be deleted without anything noticing.
	FakeHotspots hs;
	hs.place(1, "OUT", GLPoint2f(0, -5));
	hs.place(2, "IN", GLPoint2f(3, -5));
	SegmentMap segs;
	segs.put(vertical(0, 0, -5, 5));
	segs.at(0).connections.push_back(conn(1, "OUT"));
	segs.put(vertical(1, 3, -5, 5));     // parallel, different x
	segs.at(1).connections.push_back(conn(2, "IN"));
	long head = 0, next = 2;

	cl::wire::mergeSegments(segs, hs, { conn(1, "OUT"), conn(2, "IN") }, head, next);

	CHECK(segs.size() == 2);
}

TEST_CASE("a join carries the absorbed segment's connections across") {
	FakeHotspots hs;
	hs.place(1, "OUT", GLPoint2f(-5, 0));
	hs.place(2, "IN", GLPoint2f(5, 0));
	SegmentMap segs;
	segs.put(horizontal(0, 0, -5, 0));
	segs.at(0).connections.push_back(conn(1, "OUT"));
	segs.put(horizontal(1, 0, 0, 5));
	segs.at(1).connections.push_back(conn(2, "IN"));
	long head = 0, next = 2;

	cl::wire::mergeSegments(segs, hs, { conn(1, "OUT"), conn(2, "IN") }, head, next);

	REQUIRE(segs.size() == 1);
	CHECK(segs.begin()->second.connections.size() == 2);
	CHECK(segs.begin()->second.begin.x == -5);
	CHECK(segs.begin()->second.end.x == 5);
}

TEST_CASE("a join carries the absorbed segment's crossings across") {
	FakeHotspots hs;
	hs.place(1, "OUT", GLPoint2f(-5, 0));
	SegmentMap segs;
	segs.put(horizontal(0, 0, -5, 0));
	segs.at(0).connections.push_back(conn(1, "OUT"));
	segs.put(horizontal(1, 0, 0, 5));
	segs.at(1).intersects[3.0f].push_back(2);   // the crossing is on the absorbed one
	segs.put(vertical(2, 3, -2, 2));
	long head = 0, next = 3;

	cl::wire::mergeSegments(segs, hs, { conn(1, "OUT") }, head, next);

	REQUIRE(segs.has(0));
	CHECK(segs.at(0).intersects.count(3.0f) == 1);
}

TEST_CASE("a wire whose segments have all collapsed keeps its connections") {
	// The all-collapsed rebuild. No capture reaches it either, because a routed
	// wire never arrives in this state.
	FakeHotspots hs;
	SegmentMap segs;
	segs.put(horizontal(4, 0, 0, 0));
	segs.put(vertical(9, 0, 0, 0));
	long head = 4, next = 10;

	cl::wire::removeZeroLengthSegments(segs, { conn(1, "OUT"), conn(2, "IN") }, head, next);

	REQUIRE(segs.size() == 1);
	CHECK(head == 0);
	CHECK(next == 1);
	CHECK(segs.at(0).connections.size() == 2);
}

TEST_CASE("a collapsed segment hands its connections to a neighbour") {
	SegmentMap segs;
	segs.put(horizontal(0, 0, 0, 0));           // collapsed
	segs.at(0).connections.push_back(conn(1, "OUT"));
	segs.at(0).intersects[0.0f].push_back(1);
	segs.put(vertical(1, 0, 0, 5));             // has length
	long head = 0, next = 2;

	cl::wire::removeZeroLengthSegments(segs, {}, head, next);

	CHECK_FALSE(segs.has(0));
	CHECK(segs.at(1).connections.size() == 1);
	CHECK(head == 1);
}

TEST_CASE("a collapsed segment with nowhere to put its connections is kept") {
	// An untidy segment beats a gate that has come unattached, so this one has
	// to survive: it has a connection and no neighbour to pass it to.
	SegmentMap segs;
	segs.put(horizontal(0, 0, 0, 0));           // collapsed, no crossings at all
	segs.at(0).connections.push_back(conn(1, "OUT"));
	segs.put(vertical(1, 8, 0, 5));             // has length, but unreachable
	long head = 1, next = 2;

	cl::wire::removeZeroLengthSegments(segs, {}, head, next);

	REQUIRE(segs.has(0));
	CHECK(segs.at(0).connections.size() == 1);
	CHECK(segs.size() == 2);
}

TEST_CASE("an upright segment never joins a flat one it touches") {
	// They share a starting corner, so every test after the orientation check
	// would say yes. An elbow is not something to straighten out.
	FakeHotspots hs;
	hs.place(1, "OUT", GLPoint2f(0, 5));
	hs.place(2, "IN", GLPoint2f(5, 0));
	SegmentMap segs;
	segs.put(vertical(0, 0, 0, 5));
	segs.at(0).connections.push_back(conn(1, "OUT"));
	segs.put(horizontal(1, 0, 0, 5));
	segs.at(1).connections.push_back(conn(2, "IN"));
	long head = 0, next = 2;

	cl::wire::mergeSegments(segs, hs, { conn(1, "OUT"), conn(2, "IN") }, head, next);

	CHECK(segs.size() == 2);
}
