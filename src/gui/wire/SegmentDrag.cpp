/*****************************************************************************
   Project: CEDAR Logic Simulator

   SegmentDrag: see wire/SegmentDrag.h
*****************************************************************************/

#include "wire/SegmentDrag.h"

#include "wire/WireTopology.h"

#include <algorithm>
#include <cfloat>
#include <map>
#include <vector>

namespace cl {
namespace wire {

//	Lifts every connection off the chosen segment onto its own stub, then
//	starts dragging it. Which segment is under the mouse is the caller's to
//	work out, since that needs the collision checker.
bool beginSegDrag(long segID, WireShape &shape, const Hotspots &hs, const klsBBox &mouse) {
	wireSegment *picked = shape.segs.find(segID);
	if (picked == NULL) return false; // nothing there to drag
	shape.before = shape.segs; // store the initial mapping of the segment tree
	GLPoint2f vertex;
	// Don't mess up the pointers; just add to this vector until we don't need the pointer anymore
	vector < wireSegment > segsToAddWhenFound;
	// Check connections on the current seg, if we need to extend segments to connections then do it
	// Each connection gets a stub of its own, facing the other way to the
	// segment it is being lifted off, meeting it at the connection's position.
	const SegAxis a = picked->axis();
	for (unsigned int i = 0; i < picked->connections.size(); i++) {
		vertex = hs.coordsOf(picked->connections[i]);
		segsToAddWhenFound.push_back(wireSegment(vertex, vertex, !a.vertical, shape.nextID++));
		wireSegment &stub = segsToAddWhenFound.back();
		stub.intersects[a.across(vertex)].push_back(picked->id);
		stub.connections.push_back(picked->connections[i]);
		picked->intersects[a.along(vertex)].push_back(stub.id);
	}
	picked->connections.clear();
	shape.dragging = picked->id;
	for (unsigned int i = 0; i < segsToAddWhenFound.size(); i++) {
		shape.segs.put(segsToAddWhenFound[i]);
	}
	shape.mouse = mouse;
	return true;
}

//	The current dragging segment is moved to a new position
//	while the associated segments are added/modified to keep
//	our drag segment connected in the tree
void updateSegDrag(WireShape &shape, const Hotspots &hs, const klsBBox &mouse) {
	if (shape.dragging == -1) return; // break out on error, seg not set
	wireSegment &drag = shape.segs.at(shape.dragging);
	// A drag slides the segment across itself, so the movement is measured and
	// written on the axis the segment does not run along. The mouse edge is read
	// against the segment's own axis, which is what makes it the near edge for an
	// upright drag and the far edge for a flat one.
	const SegAxis dragAxis = drag.axis();
	const SegAxis a = dragAxis.perp();
	klsBBox newMouseCoords = mouse;
	float diff = dragAxis.acrossEdge(newMouseCoords) - dragAxis.acrossEdge(shape.mouse);
	a.along(drag.begin) += diff;
	a.along(drag.end) += diff;
	drag.calcBBox();
	refreshIntersections(shape.segs);
	// Update the other segments by extending/shrinking
	map < GLfloat, vector < long > >::iterator isectWalk = drag.intersects.begin();
	while (isectWalk != drag.intersects.end()) {
		// Cases here are if intersection is on endpoint or if intersection is in middle
		// 	if on endpoint, then shrink or grow intersected segment as necessary
		// As well, since the key to the map is x coord for horizontal segs and y coord for vertical segs...
		for (unsigned int z = 0; z < (isectWalk->second).size(); z++) {
			wireSegment* ws = shape.segs.find((isectWalk->second)[z]);
			if (ws == NULL) continue;  // stale id, never invent one
			float hsMin = FLT_MAX, hsMax = -FLT_MAX;
			// For endpoints on an intersected segment, there are three options:
			//		the dragged seg, the extreme hotspot, or the extreme intersection
			//		As always, begin is min, end is max
			// Extend/shrink the endpoints if necessary, if in the middle then no mod necessary
			for (unsigned int i = 0; i < ws->connections.size(); i++) {
				GLPoint2f hsPoint = hs.coordsOf(ws->connections[i]);
				hsMin = std::min(hsMin, a.along(hsPoint));
				hsMax = std::max(hsMax, a.along(hsPoint));
			}
			map < GLfloat, vector < long > >::iterator wsNear = ws->intersects.begin();
			float isectNear = (wsNear != ws->intersects.end() ? wsNear->first : FLT_MAX);
			map < GLfloat, vector < long > >::reverse_iterator wsFar = ws->intersects.rbegin();
			float isectFar = (wsFar != ws->intersects.rend() ? wsFar->first : -FLT_MAX);
			a.along(ws->begin) = std::min(a.along(drag.begin), hsMin);
			a.along(ws->begin) = std::min(a.along(ws->begin), isectNear);
			a.along(ws->end) = std::max(a.along(drag.begin), hsMax);
			a.along(ws->end) = std::max(a.along(ws->end), isectFar);
			ws->calcBBox();
		}
		isectWalk++;
	}

	refreshIntersections(shape.segs);

	shape.mouse = mouse;
}

//	The current dragging segment is dropped, clean up
void endSegDrag(WireShape &shape) {
	// Reset the drag segment var
	shape.dragging = -1;
}

// Update the placement of a connection by extending/moving its
//	segment.  Will set up a mouse coord from the current position
//	and another one from the new position to pass to updateSegDrag
void updateConnectionPos(unsigned long gid, const std::string &connection,
                         WireShape &shape, const Hotspots &hs) {
	// Keep this mirrored until tests cover each hotspot/segment axis and split case.
	bool foundit = false;
	GLPoint2f newLocation;
	unsigned int connID = 0;
	map < long, wireSegment >::iterator segWalk = shape.segs.begin();

	while (segWalk != shape.segs.end() && !foundit) {
		for (unsigned int j = 0; j < (segWalk->second).connections.size() && !foundit; j++) {
			if ((segWalk->second).connections[j].gid == gid && (segWalk->second).connections[j].connection == connection) {
				newLocation = hs.coordsOf((segWalk->second).connections[j]);
				foundit = true;
				shape.dragging = (segWalk->first);
				connID = j;
				break;
			}
		}
		segWalk++;
	}
	if (!foundit) return;
	klsBBox origin;
	if (!hs.isVertical(shape.segs.at(shape.dragging).connections[connID])) {
		// We found the segment we're looking for
		if (shape.segs.at(shape.dragging).isVertical()) {
			// If the seg is vertical then create a horizontal seg to handle the connection and remove the connection from the vertical seg
			shape.segs.put(wireSegment(newLocation, GLPoint2f(shape.segs.at(shape.dragging).begin.x, newLocation.y), false, shape.nextID));
			shape.segs.at(shape.nextID).intersects[shape.segs.at(shape.dragging).begin.x].push_back(shape.dragging);
			shape.segs.at(shape.nextID).connections.push_back(shape.segs.at(shape.dragging).connections[connID]);
			shape.segs.at(shape.dragging).intersects[newLocation.y].push_back(shape.nextID);
			shape.segs.at(shape.dragging).connections.erase(shape.segs.at(shape.dragging).connections.begin() + connID);
			// Now we'll handle the horizontal seg
			shape.dragging = shape.nextID;
			shape.nextID++;
			connID = 0;
		}
		// make new segs for other connections on my selected segment
		for (unsigned int j = 0; j < shape.segs.at(shape.dragging).connections.size(); j++) {
			if (j != connID) {
				GLPoint2f connPoint;
				connPoint = hs.coordsOf(shape.segs.at(shape.dragging).connections[j]);
				shape.segs.put(wireSegment(connPoint, connPoint, true, shape.nextID));
				shape.segs.at(shape.nextID).intersects[connPoint.y].push_back(shape.dragging);
				shape.segs.at(shape.nextID).connections.push_back(shape.segs.at(shape.dragging).connections[j]);
				shape.segs.at(shape.dragging).intersects[connPoint.x].push_back(shape.nextID);
				shape.nextID++;
			}
		}
		// Reseat the connection on this horizontal seg
		wireConnection wc = shape.segs.at(shape.dragging).connections[connID];
		shape.segs.at(shape.dragging).connections.clear();
		shape.segs.at(shape.dragging).connections.push_back(wc);
		// Extend/shrink the endpoints if necessary, if in the middle then no mod necessary
		GLPoint2f hsPoint;
		map < GLfloat, vector < long > >::iterator wsLeft = shape.segs.at(shape.dragging).intersects.begin();
		float isectLeft = (wsLeft != shape.segs.at(shape.dragging).intersects.end() ? wsLeft->first : FLT_MAX);
		map < GLfloat, vector < long > >::reverse_iterator wsRight = shape.segs.at(shape.dragging).intersects.rbegin();
		float isectRight = (wsRight != shape.segs.at(shape.dragging).intersects.rend() ? wsRight->first : -FLT_MAX);
		origin.addPoint(GLPoint2f(0, shape.segs.at(shape.dragging).begin.y));
		shape.mouse = origin;
		shape.segs.at(shape.dragging).begin.x = std::min(newLocation.x, isectLeft);
		shape.segs.at(shape.dragging).end.x = std::max(newLocation.x, isectRight);
		origin.reset();
		origin.addPoint(GLPoint2f(0, newLocation.y));
	}
	else {
		// We found the segment we're looking for
		if (shape.segs.at(shape.dragging).isHorizontal()) {
			// If the seg is horizontal then create a vertical seg to handle the connection and remove the connection from the horizontal seg
			shape.segs.put(wireSegment(newLocation, GLPoint2f(newLocation.x, shape.segs.at(shape.dragging).begin.y), true, shape.nextID));
			shape.segs.at(shape.nextID).intersects[shape.segs.at(shape.dragging).begin.y].push_back(shape.dragging);
			shape.segs.at(shape.nextID).connections.push_back(shape.segs.at(shape.dragging).connections[connID]);
			shape.segs.at(shape.dragging).intersects[newLocation.x].push_back(shape.nextID);
			shape.segs.at(shape.dragging).connections.erase(shape.segs.at(shape.dragging).connections.begin() + connID);
			// Now we'll handle the horizontal seg
			shape.dragging = shape.nextID;
			shape.nextID++;
			connID = 0;
		}
		// make new segs for other connections on my selected segment
		for (unsigned int j = 0; j < shape.segs.at(shape.dragging).connections.size(); j++) {
			if (j != connID) {
				GLPoint2f connPoint;
				connPoint = hs.coordsOf(shape.segs.at(shape.dragging).connections[j]);
				shape.segs.put(wireSegment(connPoint, connPoint, false, shape.nextID));
				shape.segs.at(shape.nextID).intersects[connPoint.x].push_back(shape.dragging);
				shape.segs.at(shape.nextID).connections.push_back(shape.segs.at(shape.dragging).connections[j]);
				shape.segs.at(shape.dragging).intersects[connPoint.y].push_back(shape.nextID);
				shape.nextID++;
			}
		}
		// Reseat the connection on this vertical seg
		wireConnection wc = shape.segs.at(shape.dragging).connections[connID];
		shape.segs.at(shape.dragging).connections.clear();
		shape.segs.at(shape.dragging).connections.push_back(wc);
		// Extend/shrink the endpoints if necessary, if in the middle then no mod necessary
		GLPoint2f hsPoint;
		map < GLfloat, vector < long > >::iterator wsBottom = shape.segs.at(shape.dragging).intersects.begin();
		float isectBottom = (wsBottom != shape.segs.at(shape.dragging).intersects.end() ? wsBottom->first : FLT_MAX);
		map < GLfloat, vector < long > >::reverse_iterator wsTop = shape.segs.at(shape.dragging).intersects.rbegin();
		float isectTop = (wsTop != shape.segs.at(shape.dragging).intersects.rend() ? wsTop->first : -FLT_MAX);
		origin.addPoint(GLPoint2f(shape.segs.at(shape.dragging).begin.x, 0));
		shape.mouse = origin;
		shape.segs.at(shape.dragging).begin.y = std::min(newLocation.y, isectBottom);
		shape.segs.at(shape.dragging).end.y = std::max(newLocation.y, isectTop);
		origin.reset();
		origin.addPoint(GLPoint2f(newLocation.x, 0));
	}
	shape.segs.at(shape.dragging).calcBBox();
	refreshIntersections(shape.segs);
	// Let updateSegDrag figure out other segments for us
	updateSegDrag(shape, hs, origin);
}

}  // namespace wire
}  // namespace cl
