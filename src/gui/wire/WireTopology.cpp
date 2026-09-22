/*****************************************************************************
   Project: CEDAR Logic Simulator

   WireTopology: see wire/WireTopology.h
*****************************************************************************/

#include "wire/WireTopology.h"

#include <map>
#include <vector>

namespace cl {
namespace wire {

void refreshIntersections(SegmentMap &segs) {
	SegmentMap::Store::iterator segWalk = segs.begin();
	while (segWalk != segs.end()) {
		std::map< GLfloat, std::vector< long > > refreshMap;
		std::map< GLfloat, std::vector< long > >::iterator isectWalk =
			(segWalk->second).intersects.begin();
		while (isectWalk != (segWalk->second).intersects.end()) {
			for (unsigned int j = 0; j < (isectWalk->second).size(); j++) {
				// Resolve without inventing; a stale id is dropped.
				const wireSegment *target = segs.find((isectWalk->second)[j]);
				if (target == NULL) continue;
				if ((segWalk->second).isVertical()) refreshMap[target->begin.y].push_back((isectWalk->second)[j]);
				else refreshMap[target->begin.x].push_back((isectWalk->second)[j]);
			}
			isectWalk++;
		}
		// ... and assign the new map
		(segWalk->second).intersects = refreshMap;
		segWalk++;
	}
}

}  // namespace wire
}  // namespace cl
