/*****************************************************************************
   Project: CEDAR Logic Simulator

   WireTopology: see wire/WireTopology.h
*****************************************************************************/

#include "wire/WireTopology.h"

#include <algorithm>
#include <cfloat>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace cl {
namespace wire {

// Take out the flaring segments of length zero.  They are so annoying that I am
// dedicating (as you can see) a function to their ultimate horrible deaths.
void removeZeroLengthSegments(SegmentMap &segs,
                              const std::vector< wireConnection > &connections,
                              long &headSegment, long &nextSegID) {
	// Nothing to tidy, and newSegMap[headSegment] below would invent a segment.
	if (segs.empty()) return;

	SegmentMap newSegMap = segs; // Start with a copy of the segment map; I really don't trust these buggers
	std::map< long, wireSegment >::iterator segWalk = newSegMap.begin();
	std::vector< long > eraseIDs; // hold a list of IDs we need to bomb
	// One special case is that all the stupid segments could be zero length...
	bool allZeroLength = true;
	while (segWalk != newSegMap.end() && allZeroLength) {
		allZeroLength = ((segWalk->second).begin == (segWalk->second).end);
		segWalk++;
	}
	if (allZeroLength) {
		// The head may already be gone; any surviving segment will do as the
		// template, and they are all the same point by definition here.
		const wireSegment *head = newSegMap.find(headSegment);
		wireSegment base = (head != NULL) ? *head : newSegMap.begin()->second;
		newSegMap.clear();
		base.id = headSegment = 0; // reset head pointer id
		base.intersects.clear(); // no intersects
		base.connections = connections; // and all connections
		newSegMap.put(base).calcBBox();
		nextSegID = 1; // reset the new seg id
		segs = std::move(newSegMap);
		return;
	}

	segWalk = newSegMap.begin();
	while (segWalk != newSegMap.end()) {
		// We can ignore segments of length greater than zero (yeah, really)
		if (!((segWalk->second).begin == (segWalk->second).end)) { segWalk++; continue; }
		// An untidy segment beats a gate that has come unattached.
		if (!rehomeConnections(newSegMap, segWalk->first)) { segWalk++; continue; }
		eraseIDs.push_back(segWalk->first);
		segWalk++;
	}
	// DIE A HORRIBLE AND REVOLTING DEATH IN THE DIGITAL DUSTBIN!!!
	for (unsigned int i = 0; i < eraseIDs.size(); i++) newSegMap.erase(eraseIDs[i]);
	if (newSegMap.empty()) return; // nothing survived; keep the shape we had
	segs = std::move(newSegMap);
	// Now make sure the intersection maps do not refer to the woebegone segments
	refreshIntersections(segs);
	// Maybe we removed the head?
	headSegment = segs.begin()->first;
}

// Take existing segments and merge concurrent segments
void mergeSegments(SegmentMap &segs, const Hotspots &hs,
                   const std::vector< wireConnection > &connections,
                   long &headSegment, long &nextSegID) {
	// NOTE: In removing a connection, we may have only one seg left,
	//	but endpoints need to be trimmed.  In this case, the code is
	//	already here, and a single pass through the loop is a small
	//	price.  After the main loop, the trip loop will finish it.
//	if (segs.size() == 1) return; // If there's only one seg, whom will I merge with?

	removeZeroLengthSegments(segs, connections, headSegment, nextSegID); // To return from H-E-double-hockeysticks
	std::map< long, wireSegment > newSegMap; // holds the new segment map that contains merged segments
	std::map< long, long > mapIDs; // maps old ids to new ids

	std::map< long, wireSegment >::iterator segWalk = segs.begin();

	while (segWalk != segs.end()) {
		wireSegment* cSeg = &(segWalk->second);
		// Taken from the segment the walk started on, and not from cSeg: a merge
		// reassigns cSeg to the segment it joined, and the two are the same
		// orientation by construction, so asking again would only obscure that.
		const SegAxis a = cSeg->axis();
		bool found = false;

		// Walk the list of new segments to see if we need to merge with any of them
		//	We'll walk the whole list but at most two merges will be performed on
		//	any segment (one on either side)
		// Once cSeg is merged with one seg in the map, setting this flag will enable
		//	merging with a seg on the other side (internal to the new seg map).
		bool mergingInMap = false;
		std::map< long, wireSegment >::iterator walkNewSegs = newSegMap.begin();
		while (walkNewSegs != newSegMap.end()) {
			wireSegment* nSeg = &(walkNewSegs->second);
			// Only merge with segs of same orientation
			if (!a.matches(*nSeg)) { walkNewSegs++; continue; }
			// Now check channel, if not the same then don't bother
			if (a.across(cSeg->begin) != a.across(nSeg->begin)) { walkNewSegs++; continue; }
			// Now a valid check can be made on endpoints.  Consider that begin's
			//	coordinate along the axis is always less than end's
			if ((a.along(cSeg->begin) >= a.along(nSeg->begin) - EQUALRANGE && a.along(cSeg->begin) <= a.along(nSeg->end) + EQUALRANGE) ||
				(a.along(cSeg->end) >= a.along(nSeg->begin) - EQUALRANGE && a.along(cSeg->end) <= a.along(nSeg->end) + EQUALRANGE) ||
				(a.along(nSeg->begin) >= a.along(cSeg->begin) - EQUALRANGE && a.along(nSeg->begin) <= a.along(cSeg->end) + EQUALRANGE) ||
				(a.along(nSeg->end) >= a.along(cSeg->begin) - EQUALRANGE && a.along(nSeg->end) <= a.along(cSeg->end) + EQUALRANGE)) {
				// Bounds are checked and the segments need merged.  Always merge to the segment
				//	already in the new seg list.  Begin point becomes min of the begin points,
				//	end point becomes max of the end points, connections are pushed on the vector
				//	and intersects are merged (ids are checked by the id map later)
				for (unsigned int i = 0; i < cSeg->connections.size(); i++)
					nSeg->connections.push_back(cSeg->connections[i]);
				std::map< GLfloat, std::vector< long > >::iterator isectWalk = cSeg->intersects.begin();
				while (isectWalk != cSeg->intersects.end()) {
					for (unsigned int i = 0; i < (isectWalk->second).size(); i++) {
						nSeg->intersects[isectWalk->first].push_back((isectWalk->second)[i]);
					}
					isectWalk++;
				}
				GLPoint2f hsPoint; float hsMin = FLT_MAX, hsMax = -FLT_MAX;
				for (unsigned int i = 0; i < nSeg->connections.size(); i++) {
					hsPoint = hs.coordsOf(nSeg->connections[i]);
					hsMin = std::min(hsMin, a.along(hsPoint));
					hsMax = std::max(hsMax, a.along(hsPoint));
				}
				// We'd better not trim endpoints here because future segments might merge on them!!
				a.along(nSeg->begin) = std::min(hsMin, a.along(nSeg->begin));
				a.along(nSeg->begin) = std::min(a.along(nSeg->begin), (nSeg->intersects.size() > 0 ? nSeg->intersects.begin()->first : FLT_MAX));
				a.along(nSeg->end) = std::max(hsMax, a.along(nSeg->end));
				a.along(nSeg->end) = std::max(a.along(nSeg->end), (nSeg->intersects.size() > 0 ? nSeg->intersects.rbegin()->first : -FLT_MAX));
				mapIDs[cSeg->id] = nSeg->id;
				if (mergingInMap) {
					// We're merging internally within the map, so get rid of the other seg
					newSegMap.erase(cSeg->id);
					break; // merged twice, so surely positively done this seg.
				}
				cSeg = nSeg;
				found = mergingInMap = true;
			}

			walkNewSegs++;
		}
		if (!found) {
			// We haven't found a merge, so add it raw
			mapIDs[segWalk->first] = segWalk->first;
			newSegMap[segWalk->first] = *cSeg;
		}

		segWalk++;
	}
	// Iron out the segment ids for intersections, and trim endpoints if necessary
	if (newSegMap.empty()) return; // nothing to iron out, and no head to name
	segWalk = newSegMap.begin();
	headSegment = (segWalk->first);
	while (segWalk != newSegMap.end()) {
		wireSegment* nSeg = &(segWalk->second);
		// trim endpoints first
		const SegAxis na = nSeg->axis();
		GLPoint2f hsPoint; float hsMin = FLT_MAX, hsMax = -FLT_MAX;
		for (unsigned int i = 0; i < nSeg->connections.size(); i++) {
			hsPoint = hs.coordsOf(nSeg->connections[i]);
			hsMin = std::min(hsMin, na.along(hsPoint));
			hsMax = std::max(hsMax, na.along(hsPoint));
		}
		if (nSeg->intersects.size() > 0) { hsMin = std::min(hsMin, nSeg->intersects.begin()->first); hsMax = std::max(hsMax, nSeg->intersects.rbegin()->first); }
		na.along(nSeg->begin) = hsMin;
		na.along(nSeg->end) = hsMax;
		// now set the intersects
		std::map< GLfloat, std::vector< long > >::iterator isectWalk = (segWalk->second).intersects.begin();
		while (isectWalk != (segWalk->second).intersects.end()) {
			std::set< long > isectSegIDs;
			isectSegIDs.insert((isectWalk->second).begin(), (isectWalk->second).end());
			(isectWalk->second).clear();
			(isectWalk->second).insert((isectWalk->second).begin(), isectSegIDs.begin(), isectSegIDs.end());
			std::vector< long > newIsectVector;
			for (unsigned int i = 0; i < (isectWalk->second).size(); i++) {
				// Resolve without inventing. Both of these are maps, so indexing
				// with an id the wire does not have would add a zero to mapIDs
				// and then a blank segment to newSegMap, quietly attaching the
				// crossing to a segment of no length, which then gets written
				// back out on the next save.
				std::map< long, long >::iterator mapped = mapIDs.find((isectWalk->second)[i]);
				if (mapped == mapIDs.end()) continue;
				std::map< long, wireSegment >::iterator target = newSegMap.find(mapped->second);
				if (target == newSegMap.end()) continue;
				if (target->second.isVertical() != (segWalk->second).isVertical()) newIsectVector.push_back(mapped->second);
			}
			(isectWalk->second) = newIsectVector;
			isectWalk++;
		}

		(segWalk->second).calcBBox();
		segWalk++;
	}

	segs = SegmentMap(std::move(newSegMap));
}

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
				refreshMap[segWalk->second.axis().along(target->begin)].push_back((isectWalk->second)[j]);
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
