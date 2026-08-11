#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "route/WireRoute.h"

using namespace cl::route;

// Find a routed segment by id (ids are stable and meaningful in the trunk model).
static const Segment *byId(const RouteResult &r, long id) {
	for (const Segment &s : r.segments) if (s.id == id) return &s;
	return nullptr;
}

// Case A: two horizontal-facing pins -> a vertical trunk at the centered x with
// one horizontal branch per pin. Expected values hand-derived from calcShape.
TEST_CASE("both-horizontal pins route to a vertical trunk with branches") {
	RouteInput in;
	in.pins = { {-3, 1, false}, {3, -1, false} };
	in.snapTrunk = true; in.nextId = 1;

	RouteResult r = TrunkRouter().route(in);

	REQUIRE(r.segments.size() == 3);
	CHECK(r.trunkPos == 0.0f);
	CHECK(r.nextId == 3);

	const Segment *trunk = byId(r, 0);
	REQUIRE(trunk);
	CHECK(trunk->vertical);
	CHECK(trunk->bx == 0.0f);           // centered x, snapped to 0.5
	CHECK(trunk->by == -1.0f);          // spans miny..maxy
	CHECK(trunk->ey == 1.0f);
	CHECK(trunk->crossings.size() == 2);

	// Pins are walked last-to-first, so pin 1 gets the first branch id.
	const Segment *b1 = byId(r, 1);
	REQUIRE(b1);
	CHECK_FALSE(b1->vertical);
	CHECK(b1->bx == 0.0f); CHECK(b1->ex == 3.0f); CHECK(b1->by == -1.0f);
	REQUIRE(b1->pins.size() == 1); CHECK(b1->pins[0] == 1);
	REQUIRE(b1->crossings.size() == 1);
	CHECK(b1->crossings[0].first == 0.0f);   // crosses the trunk at x=0
	CHECK(b1->crossings[0].second == 0);     // ...which is trunk id 0

	const Segment *b0 = byId(r, 2);
	REQUIRE(b0);
	CHECK(b0->bx == -3.0f); CHECK(b0->ex == 0.0f); CHECK(b0->by == 1.0f);
	CHECK(b0->pins[0] == 0);
}

// Case C: one vertical, one horizontal -> an L-bend (ids 0 and 1).
TEST_CASE("mixed-orientation pins route to an L-bend") {
	RouteInput in;
	in.pins = { {0, 3, true}, {2, 0, false} };
	in.nextId = 1;

	RouteResult r = TrunkRouter().route(in);

	REQUIRE(r.segments.size() == 2);
	CHECK(r.nextId == 2);

	const Segment *v = byId(r, 0);
	REQUIRE(v); CHECK(v->vertical);
	CHECK(v->bx == 0.0f); CHECK(v->by == 0.0f); CHECK(v->ey == 3.0f);
	CHECK(v->pins[0] == 0);

	const Segment *h = byId(r, 1);
	REQUIRE(h); CHECK_FALSE(h->vertical);
	CHECK(h->bx == 0.0f); CHECK(h->ex == 2.0f); CHECK(h->by == 0.0f);
	CHECK(h->pins[0] == 1);

	CHECK(v->crossings[0].second == 1);   // v crosses h
	CHECK(h->crossings[0].second == 0);   // h crosses v
}

// Case B: two vertical-facing pins -> a horizontal trunk (id 2) with vertical
// branches (ids 0,1), the mirror of case A.
TEST_CASE("both-vertical pins route to a horizontal trunk") {
	RouteInput in;
	in.pins = { {-2, 3, true}, {2, -3, true} };
	in.snapTrunk = true; in.nextId = 1;

	RouteResult r = TrunkRouter().route(in);

	REQUIRE(r.segments.size() == 3);
	CHECK(r.nextId == 3);

	const Segment *trunk = byId(r, 2);
	REQUIRE(trunk);
	CHECK_FALSE(trunk->vertical);
	CHECK(trunk->bx == -2.0f); CHECK(trunk->ex == 2.0f);
	CHECK(trunk->by == 0.0f);            // centered y

	const Segment *b = byId(r, 0);
	REQUIRE(b); CHECK(b->vertical);
	CHECK(b->bx == 2.0f); CHECK(b->by == -3.0f); CHECK(b->ey == 0.0f);
	CHECK(b->pins[0] == 1);
}

// Degenerate both-vertical (pins share a y) falls through to the case-A builder,
// exactly like calcShape's `goto fallout`.
TEST_CASE("both-vertical but collinear falls out to the horizontal builder") {
	RouteInput in;
	in.pins = { {-2, 0, true}, {2, 0, true} };
	in.snapTrunk = true; in.nextId = 1;

	RouteResult r = TrunkRouter().route(in);

	const Segment *trunk = byId(r, 0);   // id 0 = case-A vertical trunk
	REQUIRE(trunk);
	CHECK(trunk->vertical);
	CHECK(trunk->bx == 0.0f);
}

// When snapTrunk is false the trunk keeps the supplied position (a user-dragged
// trunk survives a recompute) rather than re-centering.
TEST_CASE("non-snap reuses the supplied trunk position") {
	RouteInput in;
	in.pins = { {-3, 1, false}, {3, -1, false} };
	in.snapTrunk = false; in.trunkPos = 1.5f; in.nextId = 5;

	RouteResult r = TrunkRouter().route(in);

	CHECK(r.trunkPos == 1.5f);
	CHECK(byId(r, 0)->bx == 1.5f);       // trunk at the reused x, not centered
}

// A pin whose branch would be zero-length (it already lies on the trunk line)
// attaches directly to the trunk instead of spawning a segment. With two pins,
// that happens when they share an x: the trunk centers on that x and both sit on
// it, so the whole wire is just the trunk.
TEST_CASE("pins on the trunk attach directly, no zero-length branch") {
	RouteInput in;
	in.pins = { {2, 3, false}, {2, -3, false} };  // shared x=2 -> trunk at x=2
	in.snapTrunk = true; in.nextId = 1;

	RouteResult r = TrunkRouter().route(in);

	REQUIRE(r.segments.size() == 1);      // just the trunk, no branches
	const Segment *trunk = byId(r, 0);
	REQUIRE(trunk);
	CHECK(trunk->bx == 2.0f);
	CHECK(trunk->pins.size() == 2);       // both connections ride the trunk
}

TEST_CASE("fewer than two pins yields no segments") {
	RouteInput in; in.pins = { {0, 0, false} };
	CHECK(TrunkRouter().route(in).segments.empty());
}
