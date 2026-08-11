/*****************************************************************************
   Project: CEDAR Logic Simulator

   PolylineToSegments: orthogonal polyline -> segment tree (Workstream H, 3.2c).
   See include/gui/avoid/PolylineToSegments.h.
*****************************************************************************/

#include "avoid/PolylineToSegments.h"

namespace cl { namespace avoid {

namespace {
bool samePoint(const RoutePoint& a, const RoutePoint& b) {
	return a.x == b.x && a.y == b.y;
}
} // namespace

ShapeOut polylineToSegments(const std::vector<RoutePoint>& raw) {
	// 1. Drop consecutive duplicate points (zero-length hops libavoid can emit
	//    when an endpoint coincides with a bend).
	std::vector<RoutePoint> p;
	for (const RoutePoint& q : raw)
		if (p.empty() || !samePoint(p.back(), q)) p.push_back(q);

	// 1b. Orthogonalize. libavoid's routes are orthogonal, but hyperedge junction
	//     stubs can arrive with float noise on the "flat" axis, or (rarely) a true
	//     diagonal. Snap a near-flat hop to axis-aligned; split a genuine diagonal
	//     into an L-bend. Everything downstream can then assume strict alignment.
	if (p.size() >= 2) {
		const float EPS = 1e-3f;
		std::vector<RoutePoint> o;
		o.push_back(p.front());
		for (size_t i = 1; i < p.size(); i++) {
			RoutePoint a = o.back(); // orthogonalized predecessor
			RoutePoint b = p[i];
			float dx = b.x - a.x, dy = b.y - a.y;
			if (dx < 0) dx = -dx;
			if (dy < 0) dy = -dy;
			if (dx < EPS)      b.x = a.x;                       // near-vertical
			else if (dy < EPS) b.y = a.y;                       // near-horizontal
			else               o.push_back({b.x, a.y});         // diagonal -> corner
			if (!samePoint(o.back(), b)) o.push_back(b);
		}
		p.swap(o);
	}

	// 2. Drop collinear interior points, so segments strictly alternate
	//    orientation and none is a redundant straight-through. A point p[i] is
	//    redundant iff its neighbours share its x (vertical run) or its y
	//    (horizontal run). Checked against the original neighbours, which is
	//    correct for an orthogonal path.
	if (p.size() >= 3) {
		std::vector<RoutePoint> s;
		s.push_back(p.front());
		for (size_t i = 1; i + 1 < p.size(); i++) {
			bool collinearV = (p[i - 1].x == p[i].x && p[i].x == p[i + 1].x);
			bool collinearH = (p[i - 1].y == p[i].y && p[i].y == p[i + 1].y);
			if (!collinearV && !collinearH) s.push_back(p[i]);
		}
		s.push_back(p.back());
		p.swap(s);
	}

	ShapeOut out;

	// 3. Degenerate route (0 or 1 distinct points): one zero-length segment.
	//    Vertical by convention (matches guiWire's reserved base segment).
	if (p.size() < 2) {
		RoutePoint at = p.empty() ? RoutePoint{0.0f, 0.0f} : p.front();
		SegmentOut seg;
		seg.id = 0;
		seg.vertical = true;
		seg.bx = seg.ex = at.x;
		seg.by = seg.ey = at.y;
		out.segments.push_back(seg);
		out.nextSegId = 1;
		return out;
	}

	// 4. One segment per consecutive pair; id = index, begin <= end.
	for (size_t i = 0; i + 1 < p.size(); i++) {
		const RoutePoint& a = p[i];
		const RoutePoint& b = p[i + 1];
		SegmentOut seg;
		seg.id = (long)i;
		seg.vertical = (a.x == b.x);
		seg.bx = a.x <= b.x ? a.x : b.x;
		seg.ex = a.x <= b.x ? b.x : a.x;
		seg.by = a.y <= b.y ? a.y : b.y;
		seg.ey = a.y <= b.y ? b.y : a.y;
		out.segments.push_back(seg);
	}
	out.srcSegId = out.segments.front().id;
	out.dstSegId = out.segments.back().id;
	out.nextSegId = (long)out.segments.size();

	// 5. Junctions: consecutive segments meet at the shared corner p[i+1]. Each
	//    records the other under its own varying-axis key (vertical -> y,
	//    horizontal -> x), the same doubly-stored form guiWire's intersects uses.
	for (size_t i = 0; i + 1 < out.segments.size(); i++) {
		SegmentOut& s0 = out.segments[i];
		SegmentOut& s1 = out.segments[i + 1];
		const RoutePoint& corner = p[i + 1];
		float k0 = s0.vertical ? corner.y : corner.x;
		float k1 = s1.vertical ? corner.y : corner.x;
		s0.crossings.push_back(std::make_pair(k0, s1.id));
		s1.crossings.push_back(std::make_pair(k1, s0.id));
	}

	return out;
}

}} // namespace cl::avoid
