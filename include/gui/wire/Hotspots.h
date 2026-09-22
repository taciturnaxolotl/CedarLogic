/*****************************************************************************
   Project: CEDAR Logic Simulator

   Hotspots: where a connection attaches, asked of whatever holds the gates.

   The segment tree reads a connection's position and facing and nothing else
   about a gate. Asking through this instead of reaching for the gate directly
   is what lets the tree be exercised without a circuit behind it.
*****************************************************************************/

#ifndef WIRE_HOTSPOTS_H_
#define WIRE_HOTSPOTS_H_

#include "gl_defs.h"
#include "wireSegment.h"

namespace cl {
namespace wire {

class Hotspots {
public:
	virtual ~Hotspots() {}

	// Where the connection attaches.
	virtual GLPoint2f coordsOf(const wireConnection &c) const = 0;

	// Whether it attaches facing along y rather than along x.
	virtual bool isVertical(const wireConnection &c) const = 0;
};

}  // namespace wire
}  // namespace cl

#endif /*WIRE_HOTSPOTS_H_*/
