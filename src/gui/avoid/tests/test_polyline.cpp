#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "avoid/PolylineToSegments.h"

#include <set>
#include <map>

using namespace cl::avoid;

namespace {

// --- invariant helpers shared across cases -------------------------------

// Every segment is purely horizontal or vertical, with begin <= end.
void requireAxisAligned(const ShapeOut& s) {
	for (const SegmentOut& seg : s.segments) {
		if (seg.vertical) CHECK(seg.bx == seg.ex);
		else              CHECK(seg.by == seg.ey);
		CHECK(seg.bx <= seg.ex);
		CHECK(seg.by <= seg.ey);
	}
}

// Crossings are symmetric (if A lists B, B lists A) and reference real ids.
void requireCrossingsConsistent(const ShapeOut& s) {
	std::set<long> ids;
	for (const SegmentOut& seg : s.segments) ids.insert(seg.id);

	std::map<long, std::multiset<long>> links;
	for (const SegmentOut& seg : s.segments)
		for (const auto& c : seg.crossings) {
			CHECK(ids.count(c.second) == 1);      // points at a real segment
			CHECK(c.second != seg.id);            // never itself
			links[seg.id].insert(c.second);
		}
	for (const auto& kv : links)
		for (long other : kv.second)
			CHECK(links[other].count(kv.first) >= 1); // reciprocated
}

// The segment graph (via crossings) is one connected component.
void requireConnected(const ShapeOut& s) {
	if (s.segments.empty()) return;
	std::map<long, std::vector<long>> adj;
	for (const SegmentOut& seg : s.segments) {
		adj[seg.id];
		for (const auto& c : seg.crossings) adj[seg.id].push_back(c.second);
	}
	std::set<long> seen;
	std::vector<long> stack{s.segments.front().id};
	while (!stack.empty()) {
		long n = stack.back(); stack.pop_back();
		if (!seen.insert(n).second) continue;
		for (long m : adj[n]) stack.push_back(m);
	}
	CHECK(seen.size() == s.segments.size());
}

const SegmentOut& byId(const ShapeOut& s, long id) {
	for (const SegmentOut& seg : s.segments) if (seg.id == id) return seg;
	FAIL("no segment with id ", id);
	return s.segments.front();
}

void requireAllInvariants(const ShapeOut& s) {
	REQUIRE(s.segments.size() >= 1);
	requireAxisAligned(s);
	requireCrossingsConsistent(s);
	requireConnected(s);
	// src/dst segments exist.
	bool haveSrc = false, haveDst = false;
	for (const SegmentOut& seg : s.segments) {
		if (seg.id == s.srcSegId) haveSrc = true;
		if (seg.id == s.dstSegId) haveDst = true;
	}
	CHECK(haveSrc);
	CHECK(haveDst);
}

} // namespace

TEST_CASE("straight horizontal is a single H segment") {
	ShapeOut s = polylineToSegments({{0, 0}, {10, 0}});
	REQUIRE(s.segments.size() == 1);
	CHECK_FALSE(s.segments[0].vertical);
	CHECK(s.segments[0].bx == 0);
	CHECK(s.segments[0].ex == 10);
	CHECK(s.segments[0].crossings.empty());
	CHECK(s.srcSegId == 0);
	CHECK(s.dstSegId == 0);
	requireAllInvariants(s);
}

TEST_CASE("straight vertical is a single V segment") {
	ShapeOut s = polylineToSegments({{3, 0}, {3, 8}});
	REQUIRE(s.segments.size() == 1);
	CHECK(s.segments[0].vertical);
	CHECK(s.segments[0].by == 0);
	CHECK(s.segments[0].ey == 8);
	requireAllInvariants(s);
}

TEST_CASE("reversed points are normalized to begin <= end") {
	ShapeOut s = polylineToSegments({{10, 0}, {0, 0}});
	REQUIRE(s.segments.size() == 1);
	CHECK(s.segments[0].bx == 0);
	CHECK(s.segments[0].ex == 10);
	requireAllInvariants(s);
}

TEST_CASE("single L-bend: two segments meeting at the corner") {
	// (0,0) -> (10,0) horizontal, then (10,0) -> (10,10) vertical.
	ShapeOut s = polylineToSegments({{0, 0}, {10, 0}, {10, 10}});
	REQUIRE(s.segments.size() == 2);

	const SegmentOut& h = byId(s, 0);
	const SegmentOut& v = byId(s, 1);
	CHECK_FALSE(h.vertical);
	CHECK(v.vertical);

	// Horizontal seg keys the junction by x (=10) -> seg 1.
	REQUIRE(h.crossings.size() == 1);
	CHECK(h.crossings[0].first == 10);
	CHECK(h.crossings[0].second == 1);
	// Vertical seg keys by y (=0) -> seg 0.
	REQUIRE(v.crossings.size() == 1);
	CHECK(v.crossings[0].first == 0);
	CHECK(v.crossings[0].second == 0);

	CHECK(s.srcSegId == 0);
	CHECK(s.dstSegId == 1);
	requireAllInvariants(s);
}

TEST_CASE("staircase: three alternating segments, chained junctions") {
	ShapeOut s = polylineToSegments({{0, 0}, {10, 0}, {10, 10}, {20, 10}});
	REQUIRE(s.segments.size() == 3);
	CHECK_FALSE(s.segments[0].vertical);
	CHECK(s.segments[1].vertical);
	CHECK_FALSE(s.segments[2].vertical);
	// Middle segment touches both neighbours; ends touch one each.
	CHECK(byId(s, 1).crossings.size() == 2);
	CHECK(byId(s, 0).crossings.size() == 1);
	CHECK(byId(s, 2).crossings.size() == 1);
	CHECK(s.srcSegId == 0);
	CHECK(s.dstSegId == 2);
	requireAllInvariants(s);
}

TEST_CASE("collinear interior points collapse to one segment") {
	ShapeOut s = polylineToSegments({{0, 0}, {5, 0}, {10, 0}});
	REQUIRE(s.segments.size() == 1);
	CHECK(s.segments[0].bx == 0);
	CHECK(s.segments[0].ex == 10);
	requireAllInvariants(s);
}

TEST_CASE("a long collinear run collapses fully") {
	ShapeOut s = polylineToSegments({{0, 0}, {2, 0}, {4, 0}, {6, 0}, {6, 5}});
	REQUIRE(s.segments.size() == 2);
	CHECK(byId(s, 0).ex == 6);   // the whole horizontal run is one segment
	requireAllInvariants(s);
}

TEST_CASE("duplicate consecutive points are dropped") {
	ShapeOut s = polylineToSegments({{0, 0}, {0, 0}, {10, 0}});
	REQUIRE(s.segments.size() == 1);
	CHECK(s.segments[0].ex == 10);
	requireAllInvariants(s);
}

TEST_CASE("degenerate single point yields a zero-length segment") {
	ShapeOut s = polylineToSegments({{5, 5}});
	REQUIRE(s.segments.size() == 1);
	CHECK(s.segments[0].bx == 5);
	CHECK(s.segments[0].ex == 5);
	CHECK(s.segments[0].by == 5);
	CHECK(s.segments[0].ey == 5);
	CHECK(s.srcSegId == s.dstSegId);
	requireAllInvariants(s);
}

TEST_CASE("degenerate coincident endpoints (the 3.2b zero-length route)") {
	ShapeOut s = polylineToSegments({{-94, -17}, {-94, -17}});
	REQUIRE(s.segments.size() == 1);
	CHECK(s.segments[0].bx == -94);
	CHECK(s.segments[0].ey == -17);
	requireAllInvariants(s);
}

TEST_CASE("empty input yields a valid zero-length segment at origin") {
	ShapeOut s = polylineToSegments({});
	REQUIRE(s.segments.size() == 1);
	CHECK(s.segments[0].bx == 0);
	CHECK(s.segments[0].by == 0);
	requireAllInvariants(s);
}

TEST_CASE("nextSegId is one past the highest id") {
	ShapeOut s = polylineToSegments({{0, 0}, {10, 0}, {10, 10}, {20, 10}});
	CHECK(s.nextSegId == 3);
	CHECK(s.headSegment == 0);
}

TEST_CASE("deterministic: same input twice gives identical output") {
	std::vector<RoutePoint> pts = {{0, 0}, {4, 0}, {4, 4}, {8, 4}, {8, 0}};
	ShapeOut a = polylineToSegments(pts);
	ShapeOut b = polylineToSegments(pts);
	REQUIRE(a.segments.size() == b.segments.size());
	for (size_t i = 0; i < a.segments.size(); i++) {
		CHECK(a.segments[i].id == b.segments[i].id);
		CHECK(a.segments[i].bx == b.segments[i].bx);
		CHECK(a.segments[i].ey == b.segments[i].ey);
		CHECK(a.segments[i].crossings.size() == b.segments[i].crossings.size());
	}
}

TEST_CASE("invariants hold on a complex U-shaped path") {
	ShapeOut s = polylineToSegments(
		{{0, 0}, {0, -5}, {10, -5}, {10, -5}, {10, 5}, {5, 5}, {5, 5}});
	requireAllInvariants(s);
}
