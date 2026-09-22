/*****************************************************************************
   Project: CEDAR Logic Simulator

   WireTopology: what a wire's segments do to each other, with no gate, canvas
   or collision checker involved. A wire's shape is a SegmentMap; anything
   these need to know about a gate they ask for through Hotspots.
*****************************************************************************/

#ifndef WIRE_WIRETOPOLOGY_H_
#define WIRE_WIRETOPOLOGY_H_

#include "wire/Hotspots.h"
#include "wire/SegmentMap.h"

#include <vector>

namespace cl {
namespace wire {

// Re-key every crossing list to where the crossed segments now sit, which is
// what makes a crossing still mean something after a segment has moved. An id
// naming a segment that has gone is dropped rather than left stale.
void refreshIntersections(SegmentMap &segs);

// Drop segments that have shrunk to a point, moving anything attached to them
// onto a neighbour that still has length. When every segment has collapsed the
// wire is rebuilt as a single point holding all of its connections.
void removeZeroLengthSegments(SegmentMap &segs,
                              const std::vector< wireConnection > &connections,
                              long &headSegment, long &nextSegID);

// Join segments that run along the same line, and trim every segment back to
// the span its connections and crossings actually need. `connections` is every
// connection the whole wire holds, needed to rebuild a wire whose segments have
// all collapsed.
void mergeSegments(SegmentMap &segs, const Hotspots &hs,
                   const std::vector< wireConnection > &connections,
                   long &headSegment, long &nextSegID);

}  // namespace wire
}  // namespace cl

#endif /*WIRE_WIRETOPOLOGY_H_*/
