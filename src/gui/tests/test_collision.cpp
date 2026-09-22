// Differential test for the spatial-grid broad phase in klsCollisionChecker.
// The grid only prunes pairs that are too far apart to touch; the narrow-phase
// bbox test is unchanged, so the reported overlaps must match a brute-force
// O(N^2) reference exactly. This drives the real checker across many randomized
// scenes -- including oversized "always-checked" objects, negative coordinates,
// and grid-boundary-aligned boxes -- and fails if any scene diverges.
//
// gl_shim.h is force-included by CMake so this builds without GL/GLU headers.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "klsCollisionChecker.h"

#include <cfloat>
#include <cmath>
#include <vector>
#include <set>
#include <random>
#include <utility>

namespace {

klsBBox makeBox(float x, float y, float w, float h) {
	klsBBox b;
	b.addPoint(GLPoint2f(x, y));
	b.addPoint(GLPoint2f(x + w, y + h));
	return b;
}

using Pair = std::pair<klsCollisionObject *, klsCollisionObject *>;
Pair ordered(klsCollisionObject *a, klsCollisionObject *b) {
	return (a < b) ? Pair(a, b) : Pair(b, a);
}

// Union of every object's overlap set, as an unordered pair relation.
std::set<Pair> reportedRelation(const std::vector<klsCollisionObject *> &objs) {
	std::set<Pair> rel;
	for (auto *o : objs)
		for (auto *ov : o->getOverlaps())
			rel.insert(ordered(o, ov));
	return rel;
}

// Brute-force truth using the same narrow-phase bbox test the checker uses.
std::set<Pair> bruteRelation(const std::vector<klsCollisionObject *> &objs) {
	std::set<Pair> rel;
	for (size_t i = 0; i < objs.size(); i++)
		for (size_t j = i + 1; j < objs.size(); j++)
			if (objs[i]->overlaps(objs[j]))
				rel.insert(ordered(objs[i], objs[j]));
	return rel;
}

} // namespace

TEST_CASE("grid broad phase matches brute force across random scenes") {
	std::mt19937 rng(0xC0FFEE);
	const int scenes = 3000;

	for (int s = 0; s < scenes; s++) {
		std::vector<klsCollisionObject *> objs;
		int n = 2 + rng() % 40;
		for (int i = 0; i < n; i++) {
			float x = (float)((int)(rng() % 400) - 200) / 20.0f; // [-10, 10]
			float y = (float)((int)(rng() % 400) - 200) / 20.0f;
			float w = (float)(rng() % 50) / 10.0f + 0.1f;        // [0.1, 5.1]
			float h = (float)(rng() % 50) / 10.0f + 0.1f;
			auto *o = new klsCollisionObject(COLL_GATE);
			o->setBBox(makeBox(x, y, w, h));
			objs.push_back(o);
		}
		// An oversized always-checked object (viewport/selection-box analogue).
		if (s % 3 == 0) {
			auto *o = new klsCollisionObject(COLL_VIEWPORT);
			o->setBBox(makeBox(-1000, -1000, 2000, 2000));
			objs.push_back(o);
		}
		// A pair that touches exactly on a grid-cell boundary (x = 2).
		if (s % 5 == 0) {
			auto *a = new klsCollisionObject(COLL_GATE);
			auto *b = new klsCollisionObject(COLL_GATE);
			a->setBBox(makeBox(0, 0, 2, 2));
			b->setBBox(makeBox(2, 0, 2, 2));
			objs.push_back(a);
			objs.push_back(b);
		}

		klsCollisionChecker checker;
		for (auto *o : objs) checker.addObject(o);
		checker.update(); // baseline: everything becomes "static"

		// Move each object once (no real change) so it gets checked against the
		// static rest, driving the checker to full coverage.
		for (auto *o : objs) {
			o->setBBox(o->getBBox());
			checker.update();
		}

		std::set<Pair> reported = reportedRelation(objs);
		std::set<Pair> truth = bruteRelation(objs);
		REQUIRE_MESSAGE(reported == truth, "scene " << s << " diverged");

		for (auto *o : objs) delete o;
	}
}

// The broad phase parks a box spanning too many cells in an "oversized" list
// rather than bucketing it, and the count that decides which way it goes was
// computed in int. A box wide enough to span the whole coordinate range wraps
// that count through zero, so it reads as small and gets bucketed cell by cell,
// which runs out of memory instead of returning.
TEST_CASE("a box spanning the whole coordinate range is still checked, not bucketed") {
	std::vector<klsCollisionObject *> objs;

	auto *wide = new klsCollisionObject(COLL_WIRE);
	// makeBox adds its width to x, which cannot express this span, so build it.
	klsBBox wideBox;
	wideBox.addPoint(GLPoint2f(-FLT_MAX, 0));
	wideBox.addPoint(GLPoint2f(FLT_MAX, 0));   // no extent at all
	wide->setBBox(wideBox);
	auto *here = new klsCollisionObject(COLL_GATE);
	here->setBBox(makeBox(0, 0, 5, 5));
	objs.push_back(wide);
	objs.push_back(here);

	klsCollisionChecker checker;
	for (auto *o : objs) checker.addObject(o);
	checker.update();

	for (auto *o : objs) {
		o->setBBox(o->getBBox());
		checker.update();
	}

	// The oversized fallback keeps the invariant: wide covers everything, so
	// every pair is still reported.
	CHECK(reportedRelation(objs) == bruteRelation(objs));

	for (auto *o : objs) delete o;
}

TEST_CASE("a cell index beyond what an int can hold does not become one") {
	// Casting a float out of int's range is undefined behaviour, and the box
	// above lands there before any counting happens.
	std::vector<klsCollisionObject *> objs;

	klsBBox hugeBox;
	hugeBox.addPoint(GLPoint2f(-FLT_MAX, -FLT_MAX));
	hugeBox.addPoint(GLPoint2f(FLT_MAX, FLT_MAX));
	auto *huge = new klsCollisionObject(COLL_WIRE);
	huge->setBBox(hugeBox);

	klsBBox nanBox;
	nanBox.addPoint(GLPoint2f(0, 0));
	nanBox.addPoint(GLPoint2f((float)NAN, 0));
	auto *nan = new klsCollisionObject(COLL_WIRE);
	nan->setBBox(nanBox);
	auto *here = new klsCollisionObject(COLL_GATE);
	here->setBBox(makeBox(0, 0, 5, 5));
	objs.push_back(huge);
	objs.push_back(nan);
	objs.push_back(here);

	klsCollisionChecker checker;
	for (auto *o : objs) checker.addObject(o);
	checker.update();

	for (auto *o : objs) {
		o->setBBox(o->getBBox());
		checker.update();
	}

	CHECK(reportedRelation(objs) == bruteRelation(objs));

	for (auto *o : objs) delete o;
}
