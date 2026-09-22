/*****************************************************************************
   Project: CEDAR Logic Simulator

   SegmentMap: see wire/SegmentMap.h
*****************************************************************************/

#include "wire/SegmentMap.h"

#include <set>
#include <vector>

using namespace std;

namespace cl {
namespace wire {

bool rehomeConnections(SegmentMap &segs, long deadID) {
	wireSegment *dead = segs.find(deadID);
	if (dead == NULL || dead->connections.empty()) return true;

	// Visited set because two zero-length segments can intersect each other.
	set< long > seen;
	vector< long > frontier;
	seen.insert(deadID);
	frontier.push_back(deadID);
	for (size_t i = 0; i < frontier.size(); i++) {
		wireSegment *cur = segs.find(frontier[i]);
		if (cur == NULL) continue;
		map< GLfloat, vector< long > >::iterator isect = cur->intersects.begin();
		for (; isect != cur->intersects.end(); isect++) {
			for (size_t j = 0; j < (isect->second).size(); j++) {
				const long nID = (isect->second)[j];
				wireSegment *n = segs.find(nID);
				if (n == NULL) continue;  // stale id, never invent one
				if (n->begin == n->end) {
					if (seen.insert(nID).second) frontier.push_back(nID);
					continue;
				}
				n->connections.insert(n->connections.begin(),
				                      dead->connections.begin(), dead->connections.end());
				return true;
			}
		}
	}
	return false;
}

}  // namespace wire
}  // namespace cl
