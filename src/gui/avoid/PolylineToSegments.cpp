/*****************************************************************************
   Project: CEDAR Logic Simulator

   PolylineToSegments: orthogonal polyline -> segment tree (Workstream H, 3.2c).
   See include/gui/avoid/PolylineToSegments.h.
*****************************************************************************/

#include "avoid/PolylineToSegments.h"

#include <map>
#include <cmath>

namespace cl { namespace avoid {

namespace {
bool samePoint(const RoutePoint& a, const RoutePoint& b) {
	return a.x == b.x && a.y == b.y;
}

// Dedup coincident points, orthogonalize (snap near-flat hops, split genuine
// diagonals into an L-bend), and drop collinear interior points -- so the result
// is a clean orthogonal point sequence with strictly alternating segments.
std::vector<RoutePoint> cleanPolyline(const std::vector<RoutePoint>& raw) {
	std::vector<RoutePoint> p;
	for (const RoutePoint& q : raw)
		if (p.empty() || !samePoint(p.back(), q)) p.push_back(q);

	if (p.size() >= 2) {
		const float EPS = 1e-3f;
		std::vector<RoutePoint> o;
		o.push_back(p.front());
		for (size_t i = 1; i < p.size(); i++) {
			RoutePoint a = o.back();
			RoutePoint b = p[i];
			float dx = b.x - a.x, dy = b.y - a.y;
			if (dx < 0) dx = -dx;
			if (dy < 0) dy = -dy;
			if (dx < EPS)      b.x = a.x;
			else if (dy < EPS) b.y = a.y;
			else               o.push_back({b.x, a.y});
			if (!samePoint(o.back(), b)) o.push_back(b);
		}
		p.swap(o);
	}

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
	return p;
}

// Quantize a coordinate for the shared-endpoint index (1e-3 world units), so
// junction points that coincide up to float noise land in the same bucket.
long quant(float v) { return (long)std::llround((double)v * 1000.0); }
} // namespace

ShapeOut polylineToSegments(const std::vector<RoutePoint>& raw) {
	std::vector<RoutePoint> p = cleanPolyline(raw);

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

ShapeOut mergePolylines(const std::vector<std::vector<RoutePoint>>& polylines) {
	ShapeOut out;
	long nextId = 0;

	// One segment per consecutive point pair, across every (cleaned) polyline,
	// with globally-unique ids. Crossings are added afterward from the shared-
	// endpoint index, so within-polyline corners and cross-polyline junctions are
	// handled by the same pass.
	RoutePoint anchor{0.0f, 0.0f};
	bool haveAnchor = false;
	for (const std::vector<RoutePoint>& raw : polylines) {
		std::vector<RoutePoint> p = cleanPolyline(raw);
		if (!p.empty() && !haveAnchor) { anchor = p.front(); haveAnchor = true; }
		for (size_t i = 0; i + 1 < p.size(); i++) {
			const RoutePoint& a = p[i];
			const RoutePoint& b = p[i + 1];
			SegmentOut seg;
			seg.id = nextId++;
			seg.vertical = (a.x == b.x);
			seg.bx = a.x <= b.x ? a.x : b.x;
			seg.ex = a.x <= b.x ? b.x : a.x;
			seg.by = a.y <= b.y ? a.y : b.y;
			seg.ey = a.y <= b.y ? b.y : a.y;
			out.segments.push_back(seg);
		}
	}

	// No usable geometry -> one zero-length segment (at the first point seen).
	if (out.segments.empty()) {
		SegmentOut seg;
		seg.id = 0; seg.vertical = true;
		seg.bx = seg.ex = anchor.x; seg.by = seg.ey = anchor.y;
		out.segments.push_back(seg);
		out.nextSegId = 1;
		return out;
	}

	// Index every segment endpoint; where segments of opposite orientation share
	// a point, cross-link them (the junction/corner structure).
	std::map<std::pair<long, long>, std::vector<size_t>> idx;
	for (size_t i = 0; i < out.segments.size(); i++) {
		const SegmentOut& s = out.segments[i];
		idx[std::make_pair(quant(s.bx), quant(s.by))].push_back(i);
		idx[std::make_pair(quant(s.ex), quant(s.ey))].push_back(i);
	}
	for (const auto& kv : idx) {
		const std::vector<size_t>& grp = kv.second;
		if (grp.size() < 2) continue;
		float px = (float)kv.first.first / 1000.0f;
		float py = (float)kv.first.second / 1000.0f;
		for (size_t m = 0; m < grp.size(); m++)
			for (size_t n = m + 1; n < grp.size(); n++) {
				SegmentOut& A = out.segments[grp[m]];
				SegmentOut& B = out.segments[grp[n]];
				if (A.vertical == B.vertical || A.id == B.id) continue;
				A.crossings.push_back(std::make_pair(A.vertical ? py : px, B.id));
				B.crossings.push_back(std::make_pair(B.vertical ? py : px, A.id));
			}
	}

	out.headSegment = out.segments.front().id;
	out.srcSegId = out.segments.front().id;
	out.dstSegId = out.segments.back().id;
	out.nextSegId = nextId;
	return out;
}

}} // namespace cl::avoid
