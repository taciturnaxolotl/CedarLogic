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

bool dropGateConnections(SegmentMap &segs, unsigned long gid) {
	bool gone = false;
	for (SegmentMap::Store::iterator seg = segs.begin(); seg != segs.end(); seg++) {
		vector< wireConnection > &conns = (seg->second).connections;
		// Backwards, so erasing does not step over the next one.
		for (size_t i = conns.size(); i-- > 0; ) {
			if (conns[i].gid != gid) continue;
			conns.erase(conns.begin() + i);
			gone = true;
		}
	}
	return gone;
}

long dropConnection(SegmentMap &segs, unsigned long gid, const string &pin) {
	long from = -1;
	for (SegmentMap::Store::iterator seg = segs.begin(); seg != segs.end(); seg++) {
		vector< wireConnection > &conns = (seg->second).connections;
		for (size_t i = conns.size(); i-- > 0; ) {
			if (conns[i].gid != gid || conns[i].connection != pin) continue;
			conns.erase(conns.begin() + i);
			from = seg->first;
		}
	}
	return from;
}

}  // namespace wire
}  // namespace cl
