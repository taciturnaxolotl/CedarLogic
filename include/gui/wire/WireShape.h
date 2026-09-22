/*****************************************************************************
   Project: CEDAR Logic Simulator

   WireShape: a wire's segments and the bookkeeping that goes with them.

   Bundled because they travel together. The drag code needs all of it at once,
   and a wire's undo snapshot is meaningless without the id the next segment
   will take.
*****************************************************************************/

#ifndef WIRE_WIRESHAPE_H_
#define WIRE_WIRESHAPE_H_

#include "klsCollisionChecker.h"
#include "wire/SegmentMap.h"

namespace cl {
namespace wire {

struct WireShape {
	// The wire's shape.
	SegmentMap segs;

	// The shape as it was when a drag began, so a drag can be undone.
	SegmentMap before;

	// The id the next segment will take. Starts at 1 because 0 belongs to the
	// base vertical segment.
	long nextID = 1;

	// The segment the wire's centre is taken from. 0, the base vertical one.
	long head = 0;

	// The segment being dragged, or -1 while nothing is.
	long dragging = -1;

	// Where the mouse was last seen during a drag.
	klsBBox mouse;
};

}  // namespace wire
}  // namespace cl

#endif /*WIRE_WIRESHAPE_H_*/
