// TrunkRouter -- behaviour-preserving extraction of guiWire::calcShape.
//
// The mapping from the original:
//   segMap[...]            -> RouteResult::segments (each Segment carries its id)
//   connectPoints[i]       -> pin index i (Segment::pins holds indices)
//   segMap[k].intersects   -> Segment::crossings (coord, other-seg id)
//   member nextSegID       -> RouteInput::nextId in, RouteResult::nextId out
//   member setVerticalBar  -> RouteInput::snapTrunk
//   segMap[0/2].begin      -> RouteInput::trunkPos (reused when !snapTrunk)
//
// The vertex stack + descending `counter` in calcShape pair vertex i with
// connection i, so here we walk pins from last to first and use the index as the
// pin. calcBBox()/mergeSegments() are not the router's job -- guiWire runs them
// after translating the result back into its segment map.

#include "route/WireRoute.h"
#include <algorithm>
#include <cfloat>

namespace cl {
namespace route {

namespace {
// Snap to the nearest 0.5 grid the way calcShape does: *2, truncate toward zero,
// /2. Kept bit-identical so the trunk lands exactly where it used to.
float snapHalf(float v) {
	return (float)((int)(v * 2.0f)) / 2.0f;
}
}  // namespace

RouteResult TrunkRouter::route(const RouteInput &in) const {
	RouteResult out;
	out.nextId = in.nextId;
	out.trunkPos = in.trunkPos;

	// Fewer than two pins: nothing to route (matches calcShape's early return).
	if (in.pins.size() < 2) return out;

	float minx = FLT_MAX, maxx = -FLT_MAX, miny = FLT_MAX, maxy = -FLT_MAX;
	for (const Pin &p : in.pins) {
		minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
		miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
	}

	const bool oneVertical = in.pins[0].verticalHotspot;
	const bool twoVertical = in.pins[1].verticalHotspot;

	long nextId = in.nextId;

	// Case B (both pins face vertically) collapses to case A when the pins share
	// a y (calcShape's `goto fallout`); fold that in with a flag.
	const bool bothVertical = oneVertical && twoVertical && (miny != maxy);

	if (bothVertical) {
		// Horizontal trunk (stored as id 2, as in the original), vertical branches.
		float centery = in.snapTrunk ? snapHalf((miny + maxy) / 2.0f) : in.trunkPos;
		out.trunkPos = centery;

		Segment trunk;
		trunk.id = 2; trunk.vertical = false;
		trunk.bx = minx; trunk.ex = maxx; trunk.by = trunk.ey = centery;

		std::vector<Segment> branches;
		nextId = 0;
		for (int i = (int)in.pins.size() - 1; i >= 0; i--) {
			const Pin &v = in.pins[i];
			if (std::min(v.y, centery) != std::max(v.y, centery)) {
				Segment s;
				s.id = nextId; s.vertical = true;
				s.bx = s.ex = v.x;
				s.by = std::min(v.y, centery); s.ey = std::max(v.y, centery);
				s.pins.push_back(i);
				s.crossings.push_back({ centery, 2 });
				trunk.crossings.push_back({ s.bx, s.id });
				branches.push_back(s);
				nextId++;
			} else {
				trunk.pins.push_back(i);
			}
		}
		out.segments.push_back(trunk);
		for (Segment &s : branches) out.segments.push_back(s);
		out.nextId = 3;
		return out;
	}

	if (oneVertical != twoVertical) {
		// Case C: one vertical, one horizontal -> an L-bend (ids 0 and 1).
		const int verticalConn = oneVertical ? 0 : 1;
		const int horizontalConn = oneVertical ? 1 : 0;
		const Pin &vv = in.pins[verticalConn];
		const Pin &hv = in.pins[horizontalConn];

		Segment vseg;
		vseg.id = 0; vseg.vertical = true;
		vseg.bx = vseg.ex = vv.x;
		vseg.by = std::min(vv.y, hv.y); vseg.ey = std::max(vv.y, hv.y);
		vseg.pins.push_back(verticalConn);

		Segment hseg;
		hseg.id = 1; hseg.vertical = false;
		hseg.bx = std::min(vv.x, hv.x); hseg.ex = std::max(vv.x, hv.x);
		hseg.by = hseg.ey = hv.y;
		hseg.pins.push_back(horizontalConn);

		vseg.crossings.push_back({ hseg.by, 1 });
		hseg.crossings.push_back({ vseg.bx, 0 });

		out.segments.push_back(vseg);
		out.segments.push_back(hseg);
		out.nextId = 2;
		return out;
	}

	// Case A (both horizontal, or the degenerate both-vertical fallout): vertical
	// trunk (id 0), horizontal branches.
	float centerx = in.snapTrunk ? snapHalf((minx + maxx) / 2.0f) : in.trunkPos;
	out.trunkPos = centerx;

	Segment trunk;
	trunk.id = 0; trunk.vertical = true;
	trunk.bx = trunk.ex = centerx;
	trunk.by = miny; trunk.ey = maxy;

	std::vector<Segment> branches;
	for (int i = (int)in.pins.size() - 1; i >= 0; i--) {
		const Pin &v = in.pins[i];
		if (std::min(v.x, centerx) != std::max(v.x, centerx)) {
			Segment s;
			s.id = nextId; s.vertical = false;
			s.bx = std::min(v.x, centerx); s.ex = std::max(v.x, centerx);
			s.by = s.ey = v.y;
			s.pins.push_back(i);
			s.crossings.push_back({ centerx, 0 });
			trunk.crossings.push_back({ s.by, s.id });
			branches.push_back(s);
			nextId++;
		} else {
			trunk.pins.push_back(i);
		}
	}
	out.segments.push_back(trunk);
	for (Segment &s : branches) out.segments.push_back(s);
	out.nextId = nextId;
	return out;
}

}  // namespace route
}  // namespace cl
