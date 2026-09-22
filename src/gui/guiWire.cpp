/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
					 Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   guiWire: GUI representation of wire objects
*****************************************************************************/

#include <cassert>

#include "guiWire.h"
#include "PaletteDrag.h"
#include "RenderMode.h"
#include "render/Scene.h"
#include "render/RenderStyle.h"
#include "route/WireRoute.h"
#include "wire/SegmentMap.h"
#include "Settings.h"
#include <cmath>
#include <cstring>
#include <set>
#include <stack>
#include <utility>
#include "guiGate.h"
#include "GUICircuit.h"
#include "XMLParser.h"
#include "gl_defs.h"

class MainApp;
DECLARE_APP(MainApp)

// Returns distance from p1 to p2
float lineMagnitude(GLPoint2f p1, GLPoint2f p2) {
	return sqrt(pow(p1.x - p2.x, 2) + pow(p1.y - p2.y, 2));
}

// Returns distance from point p to line defined by l1 and l2
float distanceToLine(GLPoint2f p, GLPoint2f l1, GLPoint2f l2) {

	float lineMag = lineMagnitude(l1, l2);

	if (lineMag < EQUALRANGE) {
		return FLT_MAX;
	}

	float u = (((p.x - l1.x)*(l2.x - l1.x)) + ((p.y - l1.y)*(l2.y - l1.y)));

	u = u / pow(lineMag, 2);

	if (u < EQUALRANGE || u > 1) {
		return min(lineMagnitude(p, l1), lineMagnitude(p, l2));
	}
	else {
		return lineMagnitude(p, GLPoint2f(l1.x + u*(l2.x - l1.x), l1.y + u*(l2.y - l1.y)));
	}
}


guiWire::guiWire() : klsCollisionObject(COLL_WIRE) {
	selected = false;
	setVerticalBar = true;
	// Start segs at 1, since 0 is reserved for the base vertical segment
	nextSegID = 1;
	// 0 is the base vertical segment
	segMap.put(wireSegment()).verticalSeg = true;
	// Reset state of currentDragSeg is -1
	currentDragSegment = -1;
	headSegment = 0; // since the base vertical seg is 0
	
	// By default, wires have only one line.
	// Also by default, the state of this line is HI_Z.
	ids.resize(1);
	state.resize(1, HI_Z);
}

// TJD. 9/26/2016
// Added destructor to fix memory bug after transition from mingw to windows.
// The bug showed itself by segfaulting when copying a gate with a wire selected.
// The problem was that wireSegment-s that are owned by guiWire and destroyed
// implicitly by its default destructor were being referenced in klsCollisionObject's destructor.
// There is a call to insertSubObject() that passes pointers to guiWire's wireSegments into the base class.
// This problem did not show up in mingw because gcc is too lenient about deleted data.
// gcc leaves recently deleted stuff alone, windows overwrites it immediately with arbitrary data.
guiWire::~guiWire() {
	detachSubObjects();
	detachFromCollisions();
}

// Resolve a connection's gate id to its live guiGate* via the owning circuit.
guiGate* guiWire::gateOf(const wireConnection& c) const {
	guiGate *gate = gCircuit != nullptr ? gCircuit->getGate(c.gid) : nullptr;
	assert(gate != nullptr && "wire holds a connection to a gate that no longer exists");
	return gate;
}

// Add an input connection to the wire
void guiWire::addConnection(guiGate* iGate, string connection, bool openMode) {

	wireConnection temp;
	// Fill all necessary items - need a pointer to the gate, an id for copy/paste
	temp.gid = iGate->getID();
	temp.connection = connection;
	connectPoints.push_back(temp);
	if (openMode) return; // On open, don't calc shape until the seg tree is explicity set
	if (connectPoints.size() < 3) { setVerticalBar = true; calcShape(); return; }
	this->detachSubObjects(); // prevent coll checker pointers from invalidating
	// Find the nearest segment
	GLPoint2f hsPoint;
	float minDistance = FLT_MAX; long closestSeg = headSegment;
	iGate->getHotspotCoords(connection, hsPoint.x, hsPoint.y);
	map < long, wireSegment >::iterator segWalk = segMap.begin();
	while (segWalk != segMap.end()) {
		float distance = distanceToLine(hsPoint, (segWalk->second).begin, (segWalk->second).end);
		if (distance < minDistance) {
			minDistance = distance;
			closestSeg = (segWalk->first);
		}
		segWalk++;
	}
	// closestSeg knows the nearest segment.  If it is vertical, then we just create a horizontal seg from it.
	//	But if it is horizontal, we need a vertical seg.
	//	When mergeSegments is called, extension of existing segments is accomplished.
	if (segMap.at(closestSeg).isHorizontal()) { // create the vertical seg
		if (segMap.at(closestSeg).begin == segMap.at(closestSeg).end) segMap.at(closestSeg).end.x += 1;
		segMap.put(wireSegment(GLPoint2f(hsPoint.x, min(hsPoint.y, segMap.at(closestSeg).begin.y)), GLPoint2f(hsPoint.x, max(hsPoint.y, segMap.at(closestSeg).begin.y)), true, nextSegID));
		segMap.at(closestSeg).intersects[hsPoint.x].push_back(nextSegID);
		segMap.at(nextSegID).intersects[segMap.at(closestSeg).begin.y].push_back(closestSeg);
	}
	else { // create the horizontal seg
		if (segMap.at(closestSeg).begin == segMap.at(closestSeg).end) segMap.at(closestSeg).end.y += 1;
		segMap.put(wireSegment(GLPoint2f(min(hsPoint.x, segMap.at(closestSeg).begin.x), hsPoint.y), GLPoint2f(max(hsPoint.x, segMap.at(closestSeg).begin.x), hsPoint.y), false, nextSegID));
		segMap.at(closestSeg).intersects[hsPoint.y].push_back(nextSegID);
		segMap.at(nextSegID).intersects[segMap.at(closestSeg).begin.x].push_back(closestSeg);
	}
	segMap.at(nextSegID).connections.push_back(temp);
	segMap.at(closestSeg).calcBBox();
	segMap.at(nextSegID).calcBBox();
	nextSegID++;
	// Now merge the segments just so's there's no complaints
	mergeSegments();
	this->calcBBox();
}

void guiWire::removeConnection(IDType gid, string connection) {
	// Find the connection I'm looking for and simply eradicate it
	for (unsigned int i = 0; i < connectPoints.size(); i++) {
		if (connectPoints[i].connection == connection && connectPoints[i].gid == gid) {
			connectPoints.erase(connectPoints.begin() + i);
			//calcShape();
			break;
		}
	}
	if (connectPoints.size() < 2) return;
	this->detachSubObjects(); // prevent coll checker pointers from invalidating
	// Now I need to find the segment with this thing and update the tree
	long segID = 0; bool found = false;
	map < long, wireSegment >::iterator segWalk = segMap.begin();
	while (segWalk != segMap.end() && !found) {
		for (unsigned int i = 0; i < (segWalk->second).connections.size(); i++) {
			if ((segWalk->second).connections[i].gid == gid && (segWalk->second).connections[i].connection == connection) {
				// We found the match, remove it
				segID = (segWalk->first); found = true;
				(segWalk->second).connections.erase((segWalk->second).connections.begin() + i);
				break;
			}
		}
		segWalk++;
	}

	// Now trim the segment if necessary and walk back through the tree.
	// Nothing to trim when no segment claimed the connection: segID is still 0,
	// and indexing the map with it would invent a segment.
	while (found && segMap.has(segID) &&
	       segMap.at(segID).connections.size() == 0 && segMap.at(segID).intersects.size() == 1) {
		long oldSegID = segID;
		const vector< long > &nextIDs = segMap.at(oldSegID).intersects.begin()->second;
		if (nextIDs.empty()) break;
		segID = nextIDs[0];
		if (!segMap.has(segID)) break;
		GLfloat mapKey = (segMap.at(segID).isVertical() ? segMap.at(oldSegID).begin.y : segMap.at(oldSegID).begin.x);
		for (unsigned int i = 0; i < segMap.at(segID).intersects[mapKey].size(); i++) {
			if (segMap.at(segID).intersects[mapKey][i] == oldSegID) segMap.at(segID).intersects[mapKey].erase(segMap.at(segID).intersects[mapKey].begin() + i);
		}
		segMap.erase(oldSegID);
		if (segMap.at(segID).intersects[mapKey].size() == 0) segMap.at(segID).intersects.erase(mapKey);
	}
	// Refresh the tree
	mergeSegments();
	calcBBox();
}

long guiWire::numConnections() {
	return connectPoints.size();
}

vector < wireConnection > guiWire::getConnections() {
	return connectPoints;
}

// Emit the wire's segments (state-colored) + connection dots into the Scene.
namespace {
	inline void wmixU(unsigned long long& h, unsigned long long v) {
		h = (h ^ v) * 1099511628211ULL;
	}
	inline void wmixF(unsigned long long& h, float f) {
		unsigned u; std::memcpy(&u, &f, sizeof u); wmixU(h, u);
	}
}

unsigned long long guiWire::geometryHash() const {
	unsigned long long h = 1469598103934665603ULL;
	for (const GLLine2f& seg : renderInfo.lineSegments) {
		wmixF(h, seg.begin.x); wmixF(h, seg.begin.y);
		wmixF(h, seg.end.x);   wmixF(h, seg.end.y);
	}
	return h;
}

unsigned long long guiWire::appearanceHash() const {
	unsigned long long h = geometryHash();
	wmixU(h, selected ? 1u : 0u);
	for (StateType s : state) wmixU(h, (unsigned)s);
	return h;
}

void guiWire::drawToScene(cl::render::Scene& scene,
                          const cl::render::RenderStyle& style) {
	using namespace cl::render;
	if (connectPoints.size() < 2) return;

	const bool isBus = ids.size() != 1;

	Stroke s;
	s.width = isBus ? 4.0f : 1.0f;
	if (!style.colorOutput || !style.showLiveState) {
		// Print/topology: black, weight carries bus vs net (state ignored).
		s = style.wire(WireState::Low, isBus);
	} else {
		// Screen: replicate draw()'s decimal-gradient state coloring.
		bool conflict = false, unknown = false, hiz = false;
		double redness = 0;
		for (int i = 0; i < (int)state.size(); i++) {
			switch (state[i]) {
				case ONE:      redness += pow(2.0, i); break;
				case HI_Z:     hiz = true; break;
				case UNKNOWN:  unknown = true; break;
				case CONFLICT: conflict = true; break;
				default:       break;
			}
		}
		double denom = pow(2.0, (double)state.size()) - 1;
		if (denom > 0) redness /= denom;
		if (conflict)      s.color = Color(0.0f, 1.0f, 1.0f);
		else if (unknown)  s.color = Color(0.3f, 0.3f, 1.0f);
		else if (hiz)      s.color = Color(0.0f, 0.78f, 0.0f);
		else               s.color = Color((float)redness, 0.0f, 0.0f);
	}
	s.dashed = selected && style.showSelection;

	const std::vector<GLLine2f>& segs = renderInfo.lineSegments;
	if (!segs.empty()) {
		std::vector<Point> pts;
		pts.reserve(segs.size() * 2);
		for (size_t i = 0; i < segs.size(); i++) {
			pts.push_back(Point(segs[i].begin.x, segs[i].begin.y));
			pts.push_back(Point(segs[i].end.x, segs[i].end.y));
		}
		scene.lines(&pts[0], pts.size(), s);
	}

	const float r = (float)appConfig().appSettings.wireConnRadius;
	for (size_t i = 0; i < renderInfo.intersectPoints.size(); i++)
		scene.fillCircle(Point(renderInfo.intersectPoints[i].x,
		                       renderInfo.intersectPoints[i].y), r, s.color);
	if (appConfig().appSettings.wireConnVisible)
		for (size_t i = 0; i < renderInfo.vertexPoints.size(); i++)
			scene.fillCircle(Point(renderInfo.vertexPoints[i].x,
			                       renderInfo.vertexPoints[i].y), r, s.color);
}

bool guiWire::hover(float cx, float cy, float delta) {

	// Set up the mouse as a collision object:
	klsCollisionObject mouse(COLL_MOUSEBOX);
	klsBBox mBox = mouse.getBBox();
	mBox.addPoint(GLPoint2f(cx, cy));
	mBox.extendTop(delta);
	mBox.extendBottom(delta);
	mBox.extendLeft(delta);
	mBox.extendRight(delta);
	mouse.setBBox(mBox);

	// Check if any segments collide with the mouse:
	if (this->overlaps(&mouse)) {
		CollisionGroup cg = this->checkSubsToObj(&mouse);
		if (!cg.empty()) {
			return true;
		}
	}

	return false;
}

// Return the begin point of the initial vertical bar seg segMap.at(headSegment).  All other segs
//	hold a delta to this so we know where to move them when the 
//	user shifts the whole wire
GLPoint2f guiWire::getCenter(void) {
	return segMap.at(headSegment).begin;
}

void guiWire::move(GLPoint2f origin, GLPoint2f delta) {

	// Only move if all connections are selected, else just let the updateConnectionPos
	//		figure it all out as various connections are moved
	for (unsigned int i = 0; i < connectPoints.size(); i++) {
		if (!(gateOf(connectPoints[i])->isSelected())) return;
	}

	GLPoint2f realDelta = origin + delta - segMap.at(headSegment).begin;

	map < long, wireSegment >::iterator segWalk = segMap.begin();

	// Walk the list from second seg on out to move segs by differentials
	while (segWalk != segMap.end()) {
		(segWalk->second).begin += realDelta;
		(segWalk->second).end += realDelta;
		(segWalk->second).calcBBox();
		segWalk++;
	}
	// Make sure the intersection maps have the correct points (since they moved)
	refreshIntersections();

	this->calcBBox();
}

// Create the bbox for this wire, based on
// the bboxes of the wire segments. Also,
// add the wire segments into the subObjs list:
void guiWire::calcBBox() {
	this->detachSubObjects();

	map < long, wireSegment >::iterator segWalk = segMap.begin();
	while (segWalk != segMap.end()) {
		this->insertSubObject(&(segWalk->second));
		segWalk++;
	}

	this->resetBBox();
	this->makeValidBBox();
}

// Take existing segment connections and update their map keys
void guiWire::refreshIntersections() {
	// Update the intersection maps for the new locations
	map < long, wireSegment >::iterator segWalk = segMap.begin();
	while (segWalk != segMap.end()) {
		map < GLfloat, vector < long > > refreshMap;
		map < GLfloat, vector < long > >::iterator isectWalk = (segWalk->second).intersects.begin();
		while (isectWalk != (segWalk->second).intersects.end()) {
			for (unsigned int j = 0; j < (isectWalk->second).size(); j++) {
				// Resolve without inventing; a stale id is dropped.
				const wireSegment *target = segMap.find((isectWalk->second)[j]);
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

bool guiWire::isSelected(void) {
	return selected;
};

void guiWire::select(void) { selected = true; };

void guiWire::unselect(void) { selected = false; };

void guiWire::setID(IDType nid) {
	ids[0] = nid;
}

IDType guiWire::getID() const {
	return ids[0];
}

void guiWire::setIDs(const std::vector<IDType> &ids) {
	this->ids = ids;
	this->state.resize(ids.size(), HI_Z);
}

const std::vector<IDType> & guiWire::getIDs() const {
	return ids;
}

void guiWire::setState(vector<StateType> state) {
	this->state = state;
};

void guiWire::setSubState(IDType buslineId, StateType state) {
	for (int i = 0; i < (int)this->state.size(); i++) {
		if (ids[i] == buslineId) {
			this->state[i] = state;
		}
	}
}

const vector<StateType> & guiWire::getState() const {
	return state;
};

// Save segment tree and wire info
void guiWire::saveWire(XMLParser* xparse) {
	xparse->openTag("wire");
	// Save the IDs for the wire (of course)
	xparse->openTag("ID");
	ostringstream oss;
	for (IDType id : ids) {
		oss << id << ' ';
	}
	xparse->writeTag("ID", oss.str());
	xparse->closeTag("ID");
	// Save the tree
	xparse->openTag("shape");
	// Step through the map, save each seg's info
	map < long, wireSegment >::iterator segWalk = segMap.begin();
	while (segWalk != segMap.end()) {
		if ((segWalk->second).isVertical()) xparse->openTag("vsegment");
		else xparse->openTag("hsegment");
		// ID
		oss.str(""); oss.clear();
		oss << (segWalk->second).id;
		xparse->openTag("ID");
		xparse->writeTag("ID", oss.str());
		xparse->closeTag("ID");
		// position - begin/end points
		oss.str(""); oss.clear();
		oss << (segWalk->second).begin.x << "," << (segWalk->second).begin.y << "," << (segWalk->second).end.x << "," << (segWalk->second).end.y;
		xparse->openTag("points");
		xparse->writeTag("points", oss.str());
		xparse->closeTag("points");
		// connections - gid and connection string
		for (unsigned int i = 0; i < (segWalk->second).connections.size(); i++) {
			xparse->openTag("connection");
			oss.str(""); oss.clear();
			oss << (segWalk->second).connections[i].gid;
			xparse->openTag("GID");
			xparse->writeTag("GID", oss.str());
			xparse->closeTag("GID");
			oss.str(""); oss.clear();
			oss << (segWalk->second).connections[i].connection;
			xparse->openTag("name");
			xparse->writeTag("name", oss.str());
			xparse->closeTag("name");
			xparse->closeTag("connection");
		}
		// intersections - must store the intersection map
		map < GLfloat, vector < long > >::iterator isectWalk = (segWalk->second).intersects.begin();
		while (isectWalk != (segWalk->second).intersects.end()) {
			for (unsigned int j = 0; j < (isectWalk->second).size(); j++) {
				xparse->openTag("intersection");
				oss.str(""); oss.clear();
				oss << isectWalk->first << " " << (isectWalk->second)[j];
				xparse->writeTag("intersection", oss.str());
				xparse->closeTag("intersection");
			}
			isectWalk++;
		}
		if ((segWalk->second).isVertical()) xparse->closeTag("vsegment");
		else xparse->closeTag("hsegment");
		segWalk++;
	}
	xparse->closeTag("shape");

	xparse->closeTag("wire");
}

// Save in v1.x compatible format (single wire ID)
void guiWire::saveWireLegacy(XMLParser* xparse) {
	xparse->openTag("wire");
	// Save only the first/primary ID for v1.x compatibility
	xparse->openTag("ID");
	ostringstream oss;
	oss << getID();  // Use single ID
	xparse->writeTag("ID", oss.str());
	xparse->closeTag("ID");
	// Save the tree (same as modern format)
	xparse->openTag("shape");
	map < long, wireSegment >::iterator segWalk = segMap.begin();
	while (segWalk != segMap.end()) {
		if ((segWalk->second).isVertical()) xparse->openTag("vsegment");
		else xparse->openTag("hsegment");
		// ID
		oss.str(""); oss.clear();
		oss << (segWalk->second).id;
		xparse->openTag("ID");
		xparse->writeTag("ID", oss.str());
		xparse->closeTag("ID");
		// position - begin/end points
		oss.str(""); oss.clear();
		oss << (segWalk->second).begin.x << "," << (segWalk->second).begin.y << "," << (segWalk->second).end.x << "," << (segWalk->second).end.y;
		xparse->openTag("points");
		xparse->writeTag("points", oss.str());
		xparse->closeTag("points");
		// connections - gid and connection string
		for (unsigned int i = 0; i < (segWalk->second).connections.size(); i++) {
			xparse->openTag("connection");
			oss.str(""); oss.clear();
			oss << (segWalk->second).connections[i].gid;
			xparse->openTag("GID");
			xparse->writeTag("GID", oss.str());
			xparse->closeTag("GID");
			oss.str(""); oss.clear();
			oss << (segWalk->second).connections[i].connection;
			xparse->openTag("name");
			xparse->writeTag("name", oss.str());
			xparse->closeTag("name");
			xparse->closeTag("connection");
		}
		// intersections
		map < GLfloat, vector < long > >::iterator isectWalk = (segWalk->second).intersects.begin();
		while (isectWalk != (segWalk->second).intersects.end()) {
			for (unsigned int j = 0; j < (isectWalk->second).size(); j++) {
				xparse->openTag("intersection");
				oss.str(""); oss.clear();
				oss << isectWalk->first << " " << (isectWalk->second)[j];
				xparse->writeTag("intersection", oss.str());
				xparse->closeTag("intersection");
			}
			isectWalk++;
		}
		if ((segWalk->second).isVertical()) xparse->closeTag("vsegment");
		else xparse->closeTag("hsegment");
		segWalk++;
	}
	xparse->closeTag("shape");
	xparse->closeTag("wire");
}

map < long, wireSegment > guiWire::getSegmentMap(void) { return segMap.store(); };

// Atomically swap in a new segment tree: detach the collision sub-objects (raw
// pointers into the segMap values we're about to destroy), move the new map in,
// then rebuild the sub-object registration via calcBBox. Every wholesale segMap
// replacement routes through here so a reassignment can never leave the collision
// checker holding a dangling pointer into a freed wireSegment.
void guiWire::commitSegMap(map < long, wireSegment > newSegMap) {
	this->detachSubObjects();
	segMap = cl::wire::SegmentMap(std::move(newSegMap));
	this->calcBBox();
}

void guiWire::setSegmentMap(map < long, wireSegment > newSegMap) {
	if (newSegMap.empty()) return; // no shape to adopt; keep the one we have
	commitSegMap(std::move(newSegMap));
	headSegment = ((segMap.begin())->first);
	nextSegID = ((segMap.rbegin())->first) + 1;
	endSegDrag();
};

map < long, wireSegment > guiWire::getOldSegmentMap(void) { return oldSegMap; };

// Calculates a default three-segment shape for the wire, from source to destination, squared halfway
void guiWire::calcShape() {
	this->detachSubObjects(); // prevent coll checker pointers from invalidating

	// Get rid of the old shape
	segMap.clear();

	// If there are less than 2 connect points then there is no reason to create a shape
	if (connectPoints.size() < 2) return;
	// Gather the pins for the router: each connection's hotspot coordinate and
	// whether that hotspot faces vertically. Pin index i maps back to
	// connectPoints[i].
	cl::route::RouteInput in;
	in.pins.reserve(connectPoints.size());
	for (const wireConnection &c : connectPoints) {
		cl::route::Pin p;
		gateOf(c)->getHotspotCoords(c.connection, p.x, p.y);
		p.verticalHotspot = gateOf(c)->isVerticalHotspot(c.connection);
		in.pins.push_back(p);
	}
	// addConnection always sets setVerticalBar before calling calcShape, so this
	// is effectively always a snap; trunkPos is only consulted when it isn't.
	in.snapTrunk = setVerticalBar;
	in.trunkPos = 0.0f;
	in.nextId = nextSegID;

	cl::route::RouteResult routed = cl::route::TrunkRouter().route(in);

	// Translate the routed topology back into the segment map: one wireSegment per
	// routed segment, its connections resolved from pin indices and its junctions
	// copied into the intersects map.
	for (const cl::route::Segment &rs : routed.segments) {
		wireSegment ws(GLPoint2f(rs.bx, rs.by), GLPoint2f(rs.ex, rs.ey), rs.vertical, rs.id);
		for (int pinIdx : rs.pins) ws.connections.push_back(connectPoints[pinIdx]);
		for (const std::pair<float, long> &cr : rs.crossings)
			ws.intersects[cr.first].push_back(cr.second);
		ws.calcBBox();
		segMap.put(ws);
	}
	nextSegID = routed.nextId;

	// Make sure the vertical bar is not reset unless I want it to be
	setVerticalBar = false;

	// Create the bounding box for collision checking
	mergeSegments();
	calcBBox();
}

//	Takes a mouse pointer and finds the segment in question, initializing the segment drag operation
bool guiWire::startSegDrag(klsCollisionObject* mouse) {
	oldSegMap = segMap.store(); // store the initial mapping of the segment tree
	// We should only reach this if we are hovering, so find the segment in question
	CollisionGroup cg = this->checkSubsToObj(mouse);
	// If there are no segments then we shouldn't drag one
	if (cg.size() == 0) return false;
	this->detachSubObjects(); // prevent coll checker pointers from invalidating	
	// Otherwise just grab the first one found and fix the connection points with new segments
	CollisionGroup::iterator cgWalk = cg.begin();
	GLPoint2f vertex;
	// Don't mess up the pointers; just add to this vector until we don't need the pointer anymore
	vector < wireSegment > segsToAddWhenFound;
	// Check connections on the current seg, if we need to extend segments to connections then do it
	for (unsigned int i = 0; i < ((wireSegment*)(*cgWalk))->connections.size(); i++) {
		gateOf(((wireSegment*)(*cgWalk))->connections[i])->getHotspotCoords(((wireSegment*)(*cgWalk))->connections[i].connection, vertex.x, vertex.y);
		if (((wireSegment*)(*cgWalk))->isVertical()) {
			segsToAddWhenFound.push_back(wireSegment(vertex, vertex, false, nextSegID++));
			segsToAddWhenFound[segsToAddWhenFound.size() - 1].intersects[vertex.x].push_back(((wireSegment*)(*cgWalk))->id);

			segsToAddWhenFound[segsToAddWhenFound.size() - 1].connections.push_back(((wireSegment*)(*cgWalk))->connections[i]);
			((wireSegment*)(*cgWalk))->intersects[vertex.y].push_back(segsToAddWhenFound[segsToAddWhenFound.size() - 1].id);
		}
		else { // just horizontal
			segsToAddWhenFound.push_back(wireSegment(vertex, vertex, true, nextSegID++));
			segsToAddWhenFound[segsToAddWhenFound.size() - 1].intersects[vertex.y].push_back(((wireSegment*)(*cgWalk))->id);

			segsToAddWhenFound[segsToAddWhenFound.size() - 1].connections.push_back(((wireSegment*)(*cgWalk))->connections[i]);
			((wireSegment*)(*cgWalk))->intersects[vertex.x].push_back(segsToAddWhenFound[segsToAddWhenFound.size() - 1].id);
		}
	}
	((wireSegment*)(*cgWalk))->connections.clear();
	currentDragSegment = ((wireSegment*)(*(cg.begin())))->id;
	for (unsigned int i = 0; i < segsToAddWhenFound.size(); i++) {
		segMap.put(segsToAddWhenFound[i]);
	}
	mouseCoords = mouse->getBBox();
	return true;
}

//	The current dragging segment is moved to a new position
//	while the associated segments are added/modified to keep
//	our drag segment connected in the tree
void guiWire::updateSegDrag(klsCollisionObject* mouse) {
	if (currentDragSegment == -1) return; // break out on error, seg not set
	wireSegment &drag = segMap.at(currentDragSegment);
	klsBBox newMouseCoords = mouse->getBBox();
	wireSegment oldSegmentPos = drag;
	if (drag.isVertical()) {
		float diff = newMouseCoords.getLeft() - mouseCoords.getLeft();
		drag.begin.x += diff;
		drag.end.x += diff;
	}
	else {
		float diff = newMouseCoords.getTop() - mouseCoords.getTop();
		drag.begin.y += diff;
		drag.end.y += diff;
	}
	drag.calcBBox();
	refreshIntersections();
	// Update the other segments by extending/shrinking
	map < GLfloat, vector < long > >::iterator isectWalk = drag.intersects.begin();
	while (isectWalk != drag.intersects.end()) {
		// Cases here are if intersection is on endpoint or if intersection is in middle
		// 	if on endpoint, then shrink or grow intersected segment as necessary
		// As well, since the key to the map is x coord for horizontal segs and y coord for vertical segs...
		for (unsigned int z = 0; z < (isectWalk->second).size(); z++) {
			wireSegment* ws = segMap.find((isectWalk->second)[z]);
			if (ws == NULL) continue;  // stale id, never invent one
			float hsMin = FLT_MAX, hsMax = -FLT_MAX;
			// For endpoints on an intersected segment, there are three options:
			//		the dragged seg, the extreme hotspot, or the extreme intersection
			//		As always, begin is min, end is max
			if (drag.isVertical()) {
				// Extend/shrink the endpoints if necessary, if in the middle then no mod necessary
				for (unsigned int i = 0; i < ws->connections.size(); i++) {
					GLPoint2f hsPoint;
					gateOf(ws->connections[i])->getHotspotCoords(ws->connections[i].connection, hsPoint.x, hsPoint.y);
					hsMin = min(hsMin, hsPoint.x);
					hsMax = max(hsMax, hsPoint.x);
				}
				map < GLfloat, vector < long > >::iterator wsLeft = ws->intersects.begin();
				float isectLeft = (wsLeft != ws->intersects.end() ? wsLeft->first : FLT_MAX);
				map < GLfloat, vector < long > >::reverse_iterator wsRight = ws->intersects.rbegin();
				float isectRight = (wsRight != ws->intersects.rend() ? wsRight->first : -FLT_MAX);
				ws->begin.x = min(drag.begin.x, hsMin);
				ws->begin.x = min(ws->begin.x, isectLeft);
				ws->end.x = max(drag.begin.x, hsMax);
				ws->end.x = max(ws->end.x, isectRight);
				ws->calcBBox();
			}
			else {
				// Extend/shrink the endpoints if necessary, if in the middle then no mod necessary
				for (unsigned int i = 0; i < ws->connections.size(); i++) {
					GLPoint2f hsPoint;
					gateOf(ws->connections[i])->getHotspotCoords(ws->connections[i].connection, hsPoint.x, hsPoint.y);
					hsMin = min(hsMin, hsPoint.y);
					hsMax = max(hsMax, hsPoint.y);
				}
				map < GLfloat, vector < long > >::iterator wsBottom = ws->intersects.begin();
				float isectBottom = (wsBottom != ws->intersects.end() ? wsBottom->first : FLT_MAX);
				map < GLfloat, vector < long > >::reverse_iterator wsTop = ws->intersects.rbegin();
				float isectTop = (wsTop != ws->intersects.rend() ? wsTop->first : -FLT_MAX);
				ws->begin.y = min(drag.begin.y, hsMin);
				ws->begin.y = min(ws->begin.y, isectBottom);
				ws->end.y = max(drag.begin.y, hsMax);
				ws->end.y = max(ws->end.y, isectTop);
				ws->calcBBox();
			}
		}
		isectWalk++;
	}

	refreshIntersections();

	this->calcBBox();
	mouseCoords = mouse->getBBox();

	generateRenderInfo();
}

//	The current dragging segment is dropped, clean up
void guiWire::endSegDrag() {
	this->detachSubObjects(); // prevent coll checker pointers from invalidating
	// Reset the drag segment var
	currentDragSegment = -1;
	// merge segments to get rid of messiness
	mergeSegments();
	this->calcBBox();
}

// Update the placement of a connection by extending/moving its
//	segment.  Will set up a mouse coord from the current position
//	and another one from the new position to pass to updateSegDrag
void guiWire::updateConnectionPos(unsigned long gid, string connection) {
	bool foundit = false;
	GLPoint2f newLocation;
	unsigned int connID = 0;
	map < long, wireSegment >::iterator segWalk = segMap.begin();

	while (segWalk != segMap.end() && !foundit) {
		for (unsigned int j = 0; j < (segWalk->second).connections.size() && !foundit; j++) {
			if ((segWalk->second).connections[j].gid == gid && (segWalk->second).connections[j].connection == connection) {
				gateOf((segWalk->second).connections[j])->getHotspotCoords(connection, newLocation.x, newLocation.y);
				foundit = true;
				currentDragSegment = (segWalk->first);
				connID = j;
				break;
			}
		}
		segWalk++;
	}
	if (!foundit) return;
	this->detachSubObjects(); // prevent coll checker pointers from invalidating
	klsBBox origin;
	if (!(gateOf(segMap.at(currentDragSegment).connections[connID])->isVerticalHotspot(segMap.at(currentDragSegment).connections[connID].connection))) {
		// We found the segment we're looking for
		if (segMap.at(currentDragSegment).isVertical()) {
			// If the seg is vertical then create a horizontal seg to handle the connection and remove the connection from the vertical seg
			segMap.put(wireSegment(newLocation, GLPoint2f(segMap.at(currentDragSegment).begin.x, newLocation.y), false, nextSegID));
			segMap.at(nextSegID).intersects[segMap.at(currentDragSegment).begin.x].push_back(currentDragSegment);
			segMap.at(nextSegID).connections.push_back(segMap.at(currentDragSegment).connections[connID]);
			segMap.at(currentDragSegment).intersects[newLocation.y].push_back(nextSegID);
			segMap.at(currentDragSegment).connections.erase(segMap.at(currentDragSegment).connections.begin() + connID);
			// Now we'll handle the horizontal seg
			currentDragSegment = nextSegID;
			nextSegID++;
			connID = 0;
		}
		// make new segs for other connections on my selected segment
		for (unsigned int j = 0; j < segMap.at(currentDragSegment).connections.size(); j++) {
			if (j != connID) {
				GLPoint2f connPoint;
				gateOf(segMap.at(currentDragSegment).connections[j])->getHotspotCoords(segMap.at(currentDragSegment).connections[j].connection, connPoint.x, connPoint.y);
				segMap.put(wireSegment(connPoint, connPoint, true, nextSegID));
				segMap.at(nextSegID).intersects[connPoint.y].push_back(currentDragSegment);
				segMap.at(nextSegID).connections.push_back(segMap.at(currentDragSegment).connections[j]);
				segMap.at(currentDragSegment).intersects[connPoint.x].push_back(nextSegID);
				nextSegID++;
			}
		}
		// Reseat the connection on this horizontal seg
		wireConnection wc = segMap.at(currentDragSegment).connections[connID];
		segMap.at(currentDragSegment).connections.clear();
		segMap.at(currentDragSegment).connections.push_back(wc);
		// Extend/shrink the endpoints if necessary, if in the middle then no mod necessary
		GLPoint2f hsPoint;
		map < GLfloat, vector < long > >::iterator wsLeft = segMap.at(currentDragSegment).intersects.begin();
		float isectLeft = (wsLeft != segMap.at(currentDragSegment).intersects.end() ? wsLeft->first : FLT_MAX);
		map < GLfloat, vector < long > >::reverse_iterator wsRight = segMap.at(currentDragSegment).intersects.rbegin();
		float isectRight = (wsRight != segMap.at(currentDragSegment).intersects.rend() ? wsRight->first : -FLT_MAX);
		origin.addPoint(GLPoint2f(0, segMap.at(currentDragSegment).begin.y));
		mouseCoords = origin;
		segMap.at(currentDragSegment).begin.x = min(newLocation.x, isectLeft);
		segMap.at(currentDragSegment).end.x = max(newLocation.x, isectRight);
		origin.reset();
		origin.addPoint(GLPoint2f(0, newLocation.y));
	}
	else {
		// We found the segment we're looking for
		if (segMap.at(currentDragSegment).isHorizontal()) {
			// If the seg is horizontal then create a vertical seg to handle the connection and remove the connection from the horizontal seg
			segMap.put(wireSegment(newLocation, GLPoint2f(newLocation.x, segMap.at(currentDragSegment).begin.y), true, nextSegID));
			segMap.at(nextSegID).intersects[segMap.at(currentDragSegment).begin.y].push_back(currentDragSegment);
			segMap.at(nextSegID).connections.push_back(segMap.at(currentDragSegment).connections[connID]);
			segMap.at(currentDragSegment).intersects[newLocation.x].push_back(nextSegID);
			segMap.at(currentDragSegment).connections.erase(segMap.at(currentDragSegment).connections.begin() + connID);
			// Now we'll handle the horizontal seg
			currentDragSegment = nextSegID;
			nextSegID++;
			connID = 0;
		}
		// make new segs for other connections on my selected segment
		for (unsigned int j = 0; j < segMap.at(currentDragSegment).connections.size(); j++) {
			if (j != connID) {
				GLPoint2f connPoint;
				gateOf(segMap.at(currentDragSegment).connections[j])->getHotspotCoords(segMap.at(currentDragSegment).connections[j].connection, connPoint.x, connPoint.y);
				segMap.put(wireSegment(connPoint, connPoint, false, nextSegID));
				segMap.at(nextSegID).intersects[connPoint.x].push_back(currentDragSegment);
				segMap.at(nextSegID).connections.push_back(segMap.at(currentDragSegment).connections[j]);
				segMap.at(currentDragSegment).intersects[connPoint.y].push_back(nextSegID);
				nextSegID++;
			}
		}
		// Reseat the connection on this vertical seg
		wireConnection wc = segMap.at(currentDragSegment).connections[connID];
		segMap.at(currentDragSegment).connections.clear();
		segMap.at(currentDragSegment).connections.push_back(wc);
		// Extend/shrink the endpoints if necessary, if in the middle then no mod necessary
		GLPoint2f hsPoint;
		map < GLfloat, vector < long > >::iterator wsBottom = segMap.at(currentDragSegment).intersects.begin();
		float isectBottom = (wsBottom != segMap.at(currentDragSegment).intersects.end() ? wsBottom->first : FLT_MAX);
		map < GLfloat, vector < long > >::reverse_iterator wsTop = segMap.at(currentDragSegment).intersects.rbegin();
		float isectTop = (wsTop != segMap.at(currentDragSegment).intersects.rend() ? wsTop->first : -FLT_MAX);
		origin.addPoint(GLPoint2f(segMap.at(currentDragSegment).begin.x, 0));
		mouseCoords = origin;
		segMap.at(currentDragSegment).begin.y = min(newLocation.y, isectBottom);
		segMap.at(currentDragSegment).end.y = max(newLocation.y, isectTop);
		origin.reset();
		origin.addPoint(GLPoint2f(newLocation.x, 0));
	}
	klsCollisionObject shiftLocation(COLL_MOUSEBOX);
	shiftLocation.setBBox(origin);
	segMap.at(currentDragSegment).calcBBox();
	refreshIntersections();
	// Let updateSegDrag figure out other segments for us
	updateSegDrag(&shiftLocation);
}

// Take existing segments and merge concurrent segments
void guiWire::mergeSegments() {
	// NOTE: In removing a connection, we may have only one seg left,
	//	but endpoints need to be trimmed.  In this case, the code is
	//	already here, and a single pass through the loop is a small
	//	price.  After the main loop, the trip loop will finish it.
//	if (segMap.size() == 1) return; // If there's only one seg, whom will I merge with?

	removeZeroLengthSegments(); // To return from H-E-double-hockeysticks
	map < long, wireSegment > newSegMap; // holds the new segment map that contains merged segments
	map < long, long > mapIDs; // maps old ids to new ids

	map < long, wireSegment >::iterator segWalk = segMap.begin();

	while (segWalk != segMap.end()) {
		wireSegment* cSeg = &(segWalk->second);
		bool found = false;

		// Walk the list of new segments to see if we need to merge with any of them
		//	We'll walk the whole list but at most two merges will be performed on
		//	any segment (one on either side)
		// Once cSeg is merged with one seg in the map, setting this flag will enable
		//	merging with a seg on the other side (internal to the new seg map).
		bool mergingInMap = false;
		map < long, wireSegment >::iterator walkNewSegs = newSegMap.begin();
		while (walkNewSegs != newSegMap.end()) {
			wireSegment* nSeg = &(walkNewSegs->second);
			// Only merge with segs of same orientation
			if (cSeg->isVertical() != nSeg->isVertical()) { walkNewSegs++; continue; }
			// Now check channel, if not the same then don't bother
			if (cSeg->isVertical() && (cSeg->begin.x != nSeg->begin.x)) { walkNewSegs++; continue; }
			if (cSeg->isHorizontal() && (cSeg->begin.y != nSeg->begin.y)) { walkNewSegs++; continue; }
			// Now a valid check can be made on endpoints.  Consider that for horizontal
			//	segments, begin's x is always less than end's x (same for y's in vertical)
			if (cSeg->isVertical() && ((cSeg->begin.y >= nSeg->begin.y - EQUALRANGE && cSeg->begin.y <= nSeg->end.y + EQUALRANGE) ||
				(cSeg->end.y >= nSeg->begin.y - EQUALRANGE && cSeg->end.y <= nSeg->end.y + EQUALRANGE) ||
				(nSeg->begin.y >= cSeg->begin.y - EQUALRANGE && nSeg->begin.y <= cSeg->end.y + EQUALRANGE) ||
				(nSeg->end.y >= cSeg->begin.y - EQUALRANGE && nSeg->end.y <= cSeg->end.y + EQUALRANGE))) {
				// Bounds are checked and the segments need merged.  Always merge to the segment
				//	already in the new seg list.  Begin point becomes min of the begin points,
				//	end point becomes max of the end points, connections are pushed on the vector
				//	and intersects are merged (ids are checked by the id map later)
				for (unsigned int i = 0; i < cSeg->connections.size(); i++)
					nSeg->connections.push_back(cSeg->connections[i]);
				map < GLfloat, vector< long > >::iterator isectWalk = cSeg->intersects.begin();
				while (isectWalk != cSeg->intersects.end()) {
					for (unsigned int i = 0; i < (isectWalk->second).size(); i++) {
						nSeg->intersects[isectWalk->first].push_back((isectWalk->second)[i]);
					}
					isectWalk++;
				}
				GLPoint2f hsPoint; float hsMin = FLT_MAX, hsMax = -FLT_MAX;
				for (unsigned int i = 0; i < nSeg->connections.size(); i++) {
					gateOf(nSeg->connections[i])->getHotspotCoords(nSeg->connections[i].connection, hsPoint.x, hsPoint.y);
					hsMin = min(hsMin, hsPoint.y);
					hsMax = max(hsMax, hsPoint.y);
				}
				// We'd better not trim endpoints here because future segments might merge on them!!
				nSeg->begin.y = min(hsMin, nSeg->begin.y);
				nSeg->begin.y = min(nSeg->begin.y, (nSeg->intersects.size() > 0 ? nSeg->intersects.begin()->first : FLT_MAX));
				nSeg->end.y = max(hsMax, nSeg->end.y);
				nSeg->end.y = max(nSeg->end.y, (nSeg->intersects.size() > 0 ? nSeg->intersects.rbegin()->first : -FLT_MAX));
				mapIDs[cSeg->id] = nSeg->id;
				if (mergingInMap) {
					// We're merging internally within the map, so get rid of the other seg
					newSegMap.erase(cSeg->id);
					break; // merged twice, so surely positively done this seg.
				}
				cSeg = nSeg;
				found = mergingInMap = true;
			}
			else if (cSeg->isHorizontal() && ((cSeg->begin.x >= nSeg->begin.x - EQUALRANGE && cSeg->begin.x <= nSeg->end.x + EQUALRANGE) ||
				(cSeg->end.x >= nSeg->begin.x - EQUALRANGE && cSeg->end.x <= nSeg->end.x + EQUALRANGE) ||
				(nSeg->begin.x >= cSeg->begin.x - EQUALRANGE && nSeg->begin.x <= cSeg->end.x + EQUALRANGE) ||
				(nSeg->end.x >= cSeg->begin.x - EQUALRANGE && nSeg->end.x <= cSeg->end.x + EQUALRANGE))) {
				// Bounds are checked and the segments need merged.  Always merge to the segment
				//	already in the new seg list.  Begin point becomes min of the begin points,
				//	end point becomes max of the end points, connections are pushed on the vector
				//	and intersects are merged (ids are checked by the id map later)
				for (unsigned int i = 0; i < cSeg->connections.size(); i++)
					nSeg->connections.push_back(cSeg->connections[i]);
				map < GLfloat, vector< long > >::iterator isectWalk = cSeg->intersects.begin();
				while (isectWalk != cSeg->intersects.end()) {
					for (unsigned int i = 0; i < (isectWalk->second).size(); i++) {
						nSeg->intersects[isectWalk->first].push_back((isectWalk->second)[i]);
					}
					isectWalk++;
				}
				GLPoint2f hsPoint; float hsMin = FLT_MAX, hsMax = -FLT_MAX;
				for (unsigned int i = 0; i < nSeg->connections.size(); i++) {
					gateOf(nSeg->connections[i])->getHotspotCoords(nSeg->connections[i].connection, hsPoint.x, hsPoint.y);
					hsMin = min(hsMin, hsPoint.x);
					hsMax = max(hsMax, hsPoint.x);
				}
				// We'd better not trim endpoints here because future segments might merge on them!!
				nSeg->begin.x = min(hsMin, nSeg->begin.x);
				nSeg->begin.x = min(nSeg->begin.x, (nSeg->intersects.size() > 0 ? nSeg->intersects.begin()->first : FLT_MAX));
				nSeg->end.x = max(hsMax, nSeg->end.x);
				nSeg->end.x = max(nSeg->end.x, (nSeg->intersects.size() > 0 ? nSeg->intersects.rbegin()->first : -FLT_MAX));
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
		GLPoint2f hsPoint; float hsMin = FLT_MAX, hsMax = -FLT_MAX;
		for (unsigned int i = 0; i < nSeg->connections.size(); i++) {
			gateOf(nSeg->connections[i])->getHotspotCoords(nSeg->connections[i].connection, hsPoint.x, hsPoint.y);
			if (nSeg->isHorizontal()) { hsMin = min(hsMin, hsPoint.x); hsMax = max(hsMax, hsPoint.x); }
			else { hsMin = min(hsMin, hsPoint.y); hsMax = max(hsMax, hsPoint.y); }
		}
		if (nSeg->intersects.size() > 0) { hsMin = min(hsMin, nSeg->intersects.begin()->first); hsMax = max(hsMax, nSeg->intersects.rbegin()->first); }
		if (nSeg->isVertical()) { nSeg->begin.y = hsMin; nSeg->end.y = hsMax; }
		else { nSeg->begin.x = hsMin; nSeg->end.x = hsMax; }
		// now set the intersects
		map < GLfloat, vector< long > >::iterator isectWalk = (segWalk->second).intersects.begin();
		while (isectWalk != (segWalk->second).intersects.end()) {
			set < long > isectSegIDs;
			isectSegIDs.insert((isectWalk->second).begin(), (isectWalk->second).end());
			(isectWalk->second).clear();
			(isectWalk->second).insert((isectWalk->second).begin(), isectSegIDs.begin(), isectSegIDs.end());
			vector < long > newIsectVector;
			for (unsigned int i = 0; i < (isectWalk->second).size(); i++) {
				// Resolve without inventing. Both of these are maps, so indexing
				// with an id the wire does not have would add a zero to mapIDs
				// and then a blank segment to newSegMap, quietly attaching the
				// crossing to a segment of no length, which then gets written
				// back out on the next save.
				map< long, long >::iterator mapped = mapIDs.find((isectWalk->second)[i]);
				if (mapped == mapIDs.end()) continue;
				map< long, wireSegment >::iterator target = newSegMap.find(mapped->second);
				if (target == newSegMap.end()) continue;
				if (target->second.isVertical() != (segWalk->second).isVertical()) newIsectVector.push_back(mapped->second);
			}
			(isectWalk->second) = newIsectVector;
			isectWalk++;
		}

		(segWalk->second).calcBBox();
		segWalk++;
	}

	commitSegMap(std::move(newSegMap));  // Assign the new tree

	generateRenderInfo();
}

// Take out the flaring segments of length zero.  They are so annoying that I am dedicating (as you can see) a function to
//	their ultimate horrible deaths.
void guiWire::removeZeroLengthSegments() {
	// Nothing to tidy, and newSegMap[headSegment] below would invent a segment.
	if (segMap.empty()) return;

	cl::wire::SegmentMap newSegMap = segMap; // Start with a copy of the segment map; I really don't trust these buggers
	map < long, wireSegment >::iterator segWalk = newSegMap.begin();
	vector < long > eraseIDs; // hold a list of IDs we need to bomb
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
		base.connections = this->connectPoints; // and all connections
		newSegMap.put(base).calcBBox();
		nextSegID = 1; // reset the new seg id
		commitSegMap(newSegMap.store());
		return;
	}

	segWalk = newSegMap.begin();
	while (segWalk != newSegMap.end()) {
		// We can ignore segments of length greater than zero (yeah, really)
		if (!((segWalk->second).begin == (segWalk->second).end)) { segWalk++; continue; }
		// An untidy segment beats a gate that has come unattached.
		if (!cl::wire::rehomeConnections(newSegMap, segWalk->first)) { segWalk++; continue; }
		eraseIDs.push_back(segWalk->first);
		segWalk++;
	}
	// DIE A HORRIBLE AND REVOLTING DEATH IN THE DIGITAL DUSTBIN!!!
	for (unsigned int i = 0; i < eraseIDs.size(); i++) newSegMap.erase(eraseIDs[i]);
	if (newSegMap.empty()) return; // nothing survived; keep the shape we had
	commitSegMap(newSegMap.store());
	// Now make sure the intersection maps do not refer to the woebegone segments
	refreshIntersections();
	// Maybe we removed the head?
	headSegment = segMap.begin()->first;
}

// fill out some info to avoid loss of cycles in render loop
void guiWire::generateRenderInfo() {
	float x, y;
	GLLine2f glLine;

	// clear out the old information.  this function is only called when
	//	the wire shape has changed.
	renderInfo.vertexPoints.clear();
	renderInfo.intersectPoints.clear();
	renderInfo.lineSegments.clear();

	// gate connection points
	for (unsigned int i = 0; i < connectPoints.size(); i++) {
		gateOf(connectPoints[i])->getHotspotCoords(connectPoints[i].connection, x, y);
		renderInfo.vertexPoints.push_back(GLPoint2f(x, y));
	}

	// lines and segment intersections
	map < long, wireSegment >::iterator segWalk = segMap.begin();
	while (segWalk != segMap.end()) {
		glLine.begin = GLPoint2f((segWalk->second).begin.x, (segWalk->second).begin.y);
		glLine.end = GLPoint2f((segWalk->second).end.x, (segWalk->second).end.y);

		renderInfo.lineSegments.push_back(glLine);

		// Save the intersection points for non-elbows:
		map < GLfloat, vector< long > >::iterator isectWalk = (segWalk->second).intersects.begin();
		while (isectWalk != (segWalk->second).intersects.end()) {
			if ((segWalk->second).isVertical()) {
				if (isectWalk->first == (segWalk->second).begin.y || isectWalk->first == (segWalk->second).end.y) { isectWalk++; continue; }
			}
			else {
				if (isectWalk->first == (segWalk->second).begin.x || isectWalk->first == (segWalk->second).end.x) { isectWalk++; continue; }
			}
			for (unsigned int i = 0; i < (isectWalk->second).size(); i++) {
				x = ((segWalk->second).isVertical() ? (segWalk->second).begin.x : isectWalk->first);
				y = ((segWalk->second).isVertical() ? isectWalk->first : (segWalk->second).begin.y);
				renderInfo.intersectPoints.push_back(GLPoint2f(x, y));
			}
			isectWalk++;
		}
		segWalk++;
	}
}
