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
#include "wire/SegmentDrag.h"
#include "wire/WireTopology.h"
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
	shape_.nextID = 1;
	// 0 is the base vertical segment
	shape_.segs.put(wireSegment()).verticalSeg = true;
	// Reset state of currentDragSeg is -1
	shape_.dragging = -1;
	shape_.head = 0; // since the base vertical seg is 0
	
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

GLPoint2f guiWire::GateHotspots::coordsOf(const wireConnection &c) const {
	GLPoint2f p;
	wire.gateOf(c)->getHotspotCoords(c.connection, p.x, p.y);
	return p;
}

bool guiWire::GateHotspots::isVertical(const wireConnection &c) const {
	return wire.gateOf(c)->isVerticalHotspot(c.connection);
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
	float minDistance = FLT_MAX; long closestSeg = shape_.head;
	iGate->getHotspotCoords(connection, hsPoint.x, hsPoint.y);
	map < long, wireSegment >::iterator segWalk = shape_.segs.begin();
	while (segWalk != shape_.segs.end()) {
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
	if (shape_.segs.at(closestSeg).isHorizontal()) { // create the vertical seg
		if (shape_.segs.at(closestSeg).begin == shape_.segs.at(closestSeg).end) shape_.segs.at(closestSeg).end.x += 1;
		shape_.segs.put(wireSegment(GLPoint2f(hsPoint.x, min(hsPoint.y, shape_.segs.at(closestSeg).begin.y)), GLPoint2f(hsPoint.x, max(hsPoint.y, shape_.segs.at(closestSeg).begin.y)), true, shape_.nextID));
		shape_.segs.at(closestSeg).intersects[hsPoint.x].push_back(shape_.nextID);
		shape_.segs.at(shape_.nextID).intersects[shape_.segs.at(closestSeg).begin.y].push_back(closestSeg);
	}
	else { // create the horizontal seg
		if (shape_.segs.at(closestSeg).begin == shape_.segs.at(closestSeg).end) shape_.segs.at(closestSeg).end.y += 1;
		shape_.segs.put(wireSegment(GLPoint2f(min(hsPoint.x, shape_.segs.at(closestSeg).begin.x), hsPoint.y), GLPoint2f(max(hsPoint.x, shape_.segs.at(closestSeg).begin.x), hsPoint.y), false, shape_.nextID));
		shape_.segs.at(closestSeg).intersects[hsPoint.y].push_back(shape_.nextID);
		shape_.segs.at(shape_.nextID).intersects[shape_.segs.at(closestSeg).begin.x].push_back(closestSeg);
	}
	shape_.segs.at(shape_.nextID).connections.push_back(temp);
	shape_.segs.at(closestSeg).calcBBox();
	shape_.segs.at(shape_.nextID).calcBBox();
	shape_.nextID++;
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
	map < long, wireSegment >::iterator segWalk = shape_.segs.begin();
	while (segWalk != shape_.segs.end() && !found) {
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
	while (found && shape_.segs.has(segID) &&
	       shape_.segs.at(segID).connections.size() == 0 && shape_.segs.at(segID).intersects.size() == 1) {
		long oldSegID = segID;
		const vector< long > &nextIDs = shape_.segs.at(oldSegID).intersects.begin()->second;
		if (nextIDs.empty()) break;
		segID = nextIDs[0];
		if (!shape_.segs.has(segID)) break;
		GLfloat mapKey = (shape_.segs.at(segID).isVertical() ? shape_.segs.at(oldSegID).begin.y : shape_.segs.at(oldSegID).begin.x);
		for (unsigned int i = 0; i < shape_.segs.at(segID).intersects[mapKey].size(); i++) {
			if (shape_.segs.at(segID).intersects[mapKey][i] == oldSegID) shape_.segs.at(segID).intersects[mapKey].erase(shape_.segs.at(segID).intersects[mapKey].begin() + i);
		}
		shape_.segs.erase(oldSegID);
		if (shape_.segs.at(segID).intersects[mapKey].size() == 0) shape_.segs.at(segID).intersects.erase(mapKey);
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

// Return the begin point of the initial vertical bar seg shape_.segs.at(shape_.head).  All other segs
//	hold a delta to this so we know where to move them when the 
//	user shifts the whole wire
GLPoint2f guiWire::getCenter(void) {
	return shape_.segs.at(shape_.head).begin;
}

void guiWire::move(GLPoint2f origin, GLPoint2f delta) {

	// Only move if all connections are selected, else just let the updateConnectionPos
	//		figure it all out as various connections are moved
	for (unsigned int i = 0; i < connectPoints.size(); i++) {
		if (!(gateOf(connectPoints[i])->isSelected())) return;
	}

	GLPoint2f realDelta = origin + delta - shape_.segs.at(shape_.head).begin;

	map < long, wireSegment >::iterator segWalk = shape_.segs.begin();

	// Walk the list from second seg on out to move segs by differentials
	while (segWalk != shape_.segs.end()) {
		(segWalk->second).begin += realDelta;
		(segWalk->second).end += realDelta;
		(segWalk->second).calcBBox();
		segWalk++;
	}
	// Make sure the intersection maps have the correct points (since they moved)
	cl::wire::refreshIntersections(shape_.segs);

	this->calcBBox();
}

// Create the bbox for this wire, based on
// the bboxes of the wire segments. Also,
// add the wire segments into the subObjs list:
void guiWire::calcBBox() {
	this->detachSubObjects();

	map < long, wireSegment >::iterator segWalk = shape_.segs.begin();
	while (segWalk != shape_.segs.end()) {
		this->insertSubObject(&(segWalk->second));
		segWalk++;
	}

	this->resetBBox();
	this->makeValidBBox();
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
	map < long, wireSegment >::iterator segWalk = shape_.segs.begin();
	while (segWalk != shape_.segs.end()) {
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
	map < long, wireSegment >::iterator segWalk = shape_.segs.begin();
	while (segWalk != shape_.segs.end()) {
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

map < long, wireSegment > guiWire::getSegmentMap(void) { return shape_.segs.store(); };

// Atomically swap in a new segment tree: detach the collision sub-objects (raw
// pointers into the shape_.segs values we're about to destroy), move the new map in,
// then rebuild the sub-object registration via calcBBox. Every wholesale shape_.segs
// replacement routes through here so a reassignment can never leave the collision
// checker holding a dangling pointer into a freed wireSegment.
void guiWire::commitSegMap(map < long, wireSegment > newSegMap) {
	this->detachSubObjects();
	shape_.segs = cl::wire::SegmentMap(std::move(newSegMap));
	this->calcBBox();
}

void guiWire::setSegmentMap(map < long, wireSegment > newSegMap) {
	if (newSegMap.empty()) return; // no shape to adopt; keep the one we have
	commitSegMap(std::move(newSegMap));
	shape_.head = ((shape_.segs.begin())->first);
	shape_.nextID = ((shape_.segs.rbegin())->first) + 1;
	endSegDrag();
};

map < long, wireSegment > guiWire::getOldSegmentMap(void) { return shape_.before.store(); };

// Calculates a default three-segment shape for the wire, from source to destination, squared halfway
void guiWire::calcShape() {
	this->detachSubObjects(); // prevent coll checker pointers from invalidating

	// Get rid of the old shape
	shape_.segs.clear();

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
	in.nextId = shape_.nextID;

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
		shape_.segs.put(ws);
	}
	shape_.nextID = routed.nextId;

	// Make sure the vertical bar is not reset unless I want it to be
	setVerticalBar = false;

	// Create the bounding box for collision checking
	mergeSegments();
	calcBBox();
}

//	Takes a mouse pointer and finds the segment in question, initializing the segment drag operation
bool guiWire::startSegDrag(klsCollisionObject* mouse) {
	// Finding which segment is under the mouse needs the collision checker, so
	// it stays here; everything after it does not.
	CollisionGroup cg = this->checkSubsToObj(mouse);
	if (cg.size() == 0) return false;
	long segID = ((wireSegment*)(*cg.begin()))->id;
	if (!shape_.segs.has(segID)) return false;
	this->detachSubObjects(); // prevent coll checker pointers from invalidating
	if (!cl::wire::beginSegDrag(segID, shape_, hotspots(), mouse->getBBox())) {
		this->calcBBox();
		return false;
	}
	this->calcBBox();
	return true;
}

//	The current dragging segment is moved to a new position
//	while the associated segments are added/modified to keep
//	our drag segment connected in the tree
void guiWire::updateSegDrag(klsCollisionObject* mouse) {
	this->detachSubObjects(); // prevent coll checker pointers from invalidating
	cl::wire::updateSegDrag(shape_, hotspots(), mouse->getBBox());
	this->calcBBox();
	generateRenderInfo();
}

//	The current dragging segment is dropped, clean up
void guiWire::endSegDrag() {
	cl::wire::endSegDrag(shape_);
	// merge segments to get rid of messiness
	mergeSegments();
}

// Update the placement of a connection by extending/moving its
//	segment.  Will set up a mouse coord from the current position
//	and another one from the new position to pass to updateSegDrag
void guiWire::updateConnectionPos(unsigned long gid, string connection) {
	this->detachSubObjects(); // prevent coll checker pointers from invalidating
	cl::wire::updateConnectionPos(gid, connection, shape_, hotspots());
	this->calcBBox();
	generateRenderInfo();
}

// Tidy the wire's shape, then put the collision checker and the render cache
// back in step with it. Detaching first is what the old in-tree commits were
// buying: the sub-objects point into segments the tidying is about to free.
void guiWire::mergeSegments() {
	this->detachSubObjects();
	cl::wire::mergeSegments(shape_.segs, hotspots(), connectPoints, shape_.head, shape_.nextID);
	this->calcBBox();
	generateRenderInfo();
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
	map < long, wireSegment >::iterator segWalk = shape_.segs.begin();
	while (segWalk != shape_.segs.end()) {
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
