/*****************************************************************************
   Project: CEDAR Logic Simulator

   WireTopology: what a wire's segments do to each other, with no gate, canvas
   or collision checker involved. A wire's shape is a SegmentMap; anything
   these need to know about a gate they ask for through Hotspots.
*****************************************************************************/

#ifndef WIRE_WIRETOPOLOGY_H_
#define WIRE_WIRETOPOLOGY_H_

#include "wire/SegmentMap.h"

namespace cl {
namespace wire {

// Re-key every crossing list to where the crossed segments now sit, which is
// what makes a crossing still mean something after a segment has moved. An id
// naming a segment that has gone is dropped rather than left stale.
void refreshIntersections(SegmentMap &segs);

}  // namespace wire
}  // namespace cl

#endif /*WIRE_WIRETOPOLOGY_H_*/
