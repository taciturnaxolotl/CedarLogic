/*****************************************************************************
   Project: CEDAR Logic Simulator

   PolylineToSegments: convert an orthogonal polyline (what libavoid's
   displayRoute() hands back) into CedarLogic's segment-tree representation --
   axis-aligned wireSegments plus the junction (intersects) links between them
   (Workstream H, phase 3.2c).

   Pure geometry: no wxWidgets, no OpenGL, no libavoid. This is the load-bearing
   translation between the router's output and the wire data model, so it lives
   as a standalone, exhaustively unit-tested function (see src/gui/avoid/tests).
   The caller adapts ShapeOut into a real map<long, wireSegment> and attaches the
   wire's pin connections to srcSegId / dstSegId.
*****************************************************************************/

#ifndef POLYLINE_TO_SEGMENTS_H_
#define POLYLINE_TO_SEGMENTS_H_

#include <vector>
#include <utility>

namespace cl { namespace avoid {

struct RoutePoint {
	float x;
	float y;
};

// One axis-aligned segment of the translated tree. begin <= end on the varying
// axis. `crossings` lists (junction-key, other-seg-id) for each segment this one
// meets at a corner; the key is this segment's varying-axis coordinate of the
// shared corner -- x for a horizontal segment, y for a vertical one -- matching
// guiWire's `intersects` map (horizontal keys by x, vertical keys by y).
struct SegmentOut {
	long id;
	bool vertical;
	float bx, by, ex, ey;
	std::vector<std::pair<float, long>> crossings;
};

// The translated tree. srcSegId / dstSegId identify the segments carrying the
// polyline's first and last points, so the caller can attach the wire's two pin
// connections to the correct segments.
struct ShapeOut {
	std::vector<SegmentOut> segments;
	long headSegment = 0;
	long nextSegId = 0;
	long srcSegId = 0;
	long dstSegId = 0;
};

// Translate an orthogonal polyline (consecutive points differ in exactly one
// axis) into a segment tree. Tolerant of duplicate and collinear points; a
// degenerate route (all points coincident) yields a single zero-length segment.
ShapeOut polylineToSegments(const std::vector<RoutePoint>& pts);

}} // namespace cl::avoid

#endif // POLYLINE_TO_SEGMENTS_H_
