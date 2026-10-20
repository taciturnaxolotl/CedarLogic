/*****************************************************************************
   Project: CEDAR Logic Simulator

   SegmentDrag: pulling one segment of a wire out of line and keeping the rest
   of the wire attached to it.

   The same deal as WireTopology: no gate, canvas or collision checker, so a
   drag can be replayed from a shape and a set of hotspots alone.
*****************************************************************************/

#ifndef WIRE_SEGMENTDRAG_H_
#define WIRE_SEGMENTDRAG_H_

#include <string>

#include "wire/Hotspots.h"
#include "wire/WireShape.h"

namespace cl {
namespace wire {

// The segment closest to `p`, measured the way hover is (a square of half-width
// `reach` around the point), or -1 when none is that close. Ties go to the lower id.
long segmentNear(const SegmentMap &segs, GLPoint2f p, float reach);

// Lift every connection off `segID` onto its own stub and start dragging it.
// The shape is snapshotted into `before` first, so a drag can be undone.
bool beginSegDrag(long segID, WireShape &shape, const Hotspots &hs, const klsBBox &mouse);

// Move the dragged segment to `mouse`, growing or shrinking the segments that
// join it so the tree stays connected. Does nothing while no drag is running.
void updateSegDrag(WireShape &shape, const Hotspots &hs, const klsBBox &mouse);

// Drop the dragged segment. Tidy and join are the caller's, so that the whole
// result reaches the collision checker and the render cache in one step.
void endSegDrag(WireShape &shape);

// A gate has moved, so reattach one of its connections: split the segment it
// sat on, give the connection a stub of its own, and drag that stub to where
// the gate now is. Ends in updateSegDrag, which finishes the neighbours.
void updateConnectionPos(unsigned long gid, const std::string &connection,
                         WireShape &shape, const Hotspots &hs);

}  // namespace wire
}  // namespace cl

#endif /*WIRE_SEGMENTDRAG_H_*/
