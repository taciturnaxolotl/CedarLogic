
// This header was pulled out of guiWire.h to keep modifications
// of guiWire from recompiling the whole project.
// Tyler J Drake 9-3-2016

#ifndef WIRE_SEGMENT_H
#define WIRE_SEGMENT_H

#include <cfloat>
#include <string>
#include <vector>
#include "logic_values.h" // StateType
#include "klsCollisionChecker.h"

class guiGate;

struct wireConnection {
	unsigned long gid; // gate id; resolved to a live guiGate* on demand via the circuit
	string connection; // know what hotspot i am connected to in the gate
};

struct SegAxis;

// guiWire's are made of wireSegments.
// Segments are vertical or horizontal.
class wireSegment : public klsCollisionObject {
public:
	// Create the bbox for this wire segment:
	void calcBBox();

	bool isHorizontal() const;

	bool isVertical() const;

	// The orientation of this segment as a value, so code that works along a
	// segment and across it can be written once instead of twice.
	SegAxis axis() const;

	// Hold the orientation of the wire segment.  Once it is initialized,
	//	the orientation does not change.
	// Initialised here because the default constructor leaves scalars
	// indeterminate, and map::operator[] default-constructs on a miss.
	bool verticalSeg = false;

	//Whenever "begin" or "end" are changed, calcBBox() must be called
	// to re-build the bounding box.

	// Endpoints of the segment.
	// Begin is always less than end.
	GLPoint2f begin;
	GLPoint2f end;

	// Keep a list of the connections that are on this segment
	vector < wireConnection > connections;

	// Keeps a sorted list of intersections with other segments
	//		For horizontal segments the key is the x value (since all y's are the same)
	//		vice versa for vertical segments, holds the id of the intersected seg
	map < GLfloat, vector < long > > intersects;

	// ID for this seg in its parent map
	long id = 0;

	wireSegment();

	// Give the segment initial values - begin and end points, and orientation
	wireSegment(GLPoint2f nB, GLPoint2f nE, bool nisVertical, unsigned long nid);
};

// Which coordinate runs along a segment and which runs across it. Carried as a
// value rather than asked of a segment, because the two are not always the same
// segment: dragging an upright one stretches the flat ones that join it, so the
// axis comes from the dragged segment while the writes land elsewhere.
struct SegAxis {
	// True when the segment runs along y.
	bool vertical = false;

	static SegAxis of(const wireSegment &s) { return SegAxis{ s.verticalSeg }; }

	SegAxis perp() const { return SegAxis{ !vertical }; }

	bool matches(const wireSegment &s) const { return s.verticalSeg == vertical; }

	GLfloat &along(GLPoint2f &p) const { return vertical ? p.y : p.x; }
	GLfloat &across(GLPoint2f &p) const { return vertical ? p.x : p.y; }
	GLfloat along(const GLPoint2f &p) const { return vertical ? p.y : p.x; }
	GLfloat across(const GLPoint2f &p) const { return vertical ? p.x : p.y; }

	// A point built from axis-relative values. GLPoint2f is always (x, y).
	GLPoint2f point(GLfloat a, GLfloat c) const {
		return vertical ? GLPoint2f(c, a) : GLPoint2f(a, c);
	}

	// The edge of a mouse box a drag measures its movement from, on the axis it
	// moves along. Not a mirror, and deliberately so: an upright segment moves
	// along x and reads the far edge, a flat one moves along y and reads the
	// near edge. Every caller passes a point box today, where the two agree, so
	// a rewrite that "corrected" this would change nothing visible and quietly
	// break the first caller that ever passes a real extent.
	GLfloat acrossEdge(klsBBox &b) const { return vertical ? b.getTop() : b.getLeft(); }
};

#endif
