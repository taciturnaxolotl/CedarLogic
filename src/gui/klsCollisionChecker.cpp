/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsCollisionChecker: Maintains bounding box overlaps
*****************************************************************************/

#include "klsCollisionChecker.h"

#include <cmath>
#include <unordered_map>

namespace {

// A uniform-grid broad phase for update(). It buckets objects by the cells their
// bounding box spans, so a query only returns objects sharing a cell with the
// query box -- a superset of the true overlaps. The caller still runs the exact
// same bbox test on each candidate, so the set of reported collisions is
// identical to the old all-against-all scan; the grid only skips pairs that
// could never touch.
//
// Correctness rests on one invariant: any two boxes that overlap share at least
// one cell, so no real collision is ever pruned. An object whose box spans an
// impractical number of cells (a viewport or selection box covering the whole
// world) is parked in an "oversized" list that every query includes, which keeps
// the invariant while avoiding a blow-up in cell count.
class UniformGrid {
public:
	explicit UniformGrid(float cellSize) : cellSize(cellSize) {}

	void insert(klsCollisionObject *obj) {
		klsBBox box = obj->getBBox();
		if (box.empty()) return; // empty boxes never overlap anything

		CellSpan s = cellsOf(box);
		if (s.tooMany) { // don't bucket a box that would touch a huge number of cells
			oversized.push_back(obj);
			return;
		}
		for (int cy = s.y0; cy <= s.y1; cy++) {
			for (int cx = s.x0; cx <= s.x1; cx++) {
				cells[key(cx, cy)].push_back(obj);
			}
		}
	}

	// Collect every object that shares a cell with box (plus all oversized
	// objects) into out. Duplicates are fine: out is a set.
	void query(const klsBBox &box, CollisionGroup &out) const {
		klsBBox b = box; // getters are non-const on klsBBox
		if (!b.empty()) {
			CellSpan s = cellsOf(b);
			if (!s.tooMany) {
				for (int cy = s.y0; cy <= s.y1; cy++) {
					for (int cx = s.x0; cx <= s.x1; cx++) {
						auto it = cells.find(key(cx, cy));
						if (it != cells.end())
							out.insert(it->second.begin(), it->second.end());
					}
				}
			} else {
				// A giant query box: fall back to every gridded object.
				for (const auto &cell : cells)
					out.insert(cell.second.begin(), cell.second.end());
			}
		}
		out.insert(oversized.begin(), oversized.end());
	}

private:
	static const long long MAX_CELLS_PER_OBJECT = 1024;

	// The inclusive cell range a box spans, plus whether that is too many cells to
	// bucket individually -- the one place the threshold is applied.
	struct CellSpan { int x0, y0, x1, y1; bool tooMany; };

	static unsigned long long key(int cx, int cy) {
		// Pack two 32-bit cell coords into one 64-bit map key.
		return ((unsigned long long)(unsigned int)cx << 32) |
		       (unsigned int)cy;
	}

	// klsBBox getters are non-const, so take the box by value.
	CellSpan cellsOf(klsBBox box) const {
		CellSpan s;
		s.x0 = (int)std::floor(box.getLeft() / cellSize);
		s.y0 = (int)std::floor(box.getBottom() / cellSize);
		s.x1 = (int)std::floor(box.getRight() / cellSize);
		s.y1 = (int)std::floor(box.getTop() / cellSize);
		s.tooMany = (long long)(s.x1 - s.x0 + 1) * (s.y1 - s.y0 + 1) > MAX_CELLS_PER_OBJECT;
		return s;
	}

	float cellSize;
	std::unordered_map<unsigned long long, std::vector<klsCollisionObject *>> cells;
	std::vector<klsCollisionObject *> oversized;
};

// Cell size in world units. Gates and hotspots are on the order of one unit, so
// this keeps most objects to a handful of cells. It only affects speed; any
// positive value yields identical collision results.
const float GRID_CELL_SIZE = 2.0f;

} // namespace

// ************************* klsCollisionObject *****************************

klsCollisionObject::klsCollisionObject(klsCollisionObjectType theType) {
	setType(theType);
	cData.bboxChanged = true; // The object is new, so mark it as having changed!
};

klsCollisionObject::~klsCollisionObject() {
	detachSubObjects();
	detachFromCollisions();
}

klsBBox klsCollisionObject::getBBox() const {
	return cData.bbox;
};

void klsCollisionObject::resetBBox() {
	cData.bbox.reset(); setBBoxChanged();
}

void klsCollisionObject::setBBox(klsBBox newBBox) {
	cData.bbox = newBBox;
	makeValidBBox(); // Make sure that the new bbox contains its children!
					 // Also can set a flag here that marks this object
					 // as "changed" for the collision system to make sure to update it.
	setBBoxChanged();
}

void klsCollisionObject::makeValidBBox() {
	CollisionGroup::iterator theObj = cData.subObjs.begin();
	while (theObj != cData.subObjs.end()) {
		cData.bbox.addBBox((*theObj)->getBBox());
		theObj++;
	}

	// NOTE: This can't use setBBox, because setBBox calls it. Therefore,
	// it must also set the "changed" flag, just in case it is called from
	// outside of setBBox.
	setBBoxChanged();
}

CollisionGroup klsCollisionObject::getSubObjects() {
	return cData.subObjs;
}

bool klsCollisionObject::overlaps(klsCollisionObject* objB) {
	return this->getBBox().overlaps(objB->getBBox());
}

// Check this object's subobjects against another obj's bbox:
// (Returns a list of subobjects of this object involved in any collisions.)
CollisionGroup klsCollisionObject::checkSubsToObj( klsCollisionObject* objB, bool resetOverlaps ) {
	CollisionGroup theObjB;
	theObjB.insert( objB );
	return klsCollisionChecker::checkGroupCollisions( this->getSubObjects(), theObjB, resetOverlaps );
}

// Check the subs of this object against the subs of another object:
// (Returns a list of subobjects of this object involved in any collisions.)
CollisionGroup klsCollisionObject::checkSubsToSubs( klsCollisionObject* objB, bool resetOverlaps ) {
	return klsCollisionChecker::checkGroupCollisions( this->getSubObjects(), objB->getSubObjects(), resetOverlaps );
}

CollisionGroup klsCollisionObject::getOverlaps() {
	return cData.overlaps;
}

klsCollisionObjectType klsCollisionObject::getType() {
	return cData.objType;
}

void klsCollisionObject::setType(klsCollisionObjectType newType) {
	cData.objType = newType;
};

void klsCollisionObject::insertSubObject(klsCollisionObject* klsc) {
	cData.subObjs.insert( klsc );
}

void klsCollisionObject::detachSubObjects() {
	CollisionGroup::iterator sub = cData.subObjs.begin();
	while( sub != cData.subObjs.end() ) {
		(*sub)->detachFromCollisions();
		sub++;
	}
	cData.subObjs.clear();
}

void klsCollisionObject::detachFromCollisions() {
	CollisionGroup badOverlaps = this->getOverlaps();
	CollisionGroup::iterator remOver = badOverlaps.begin();
	while( remOver != badOverlaps.end() ) {
		(*remOver)->removeOverlap( this );
		remOver++;
	}
	this->clearOverlaps();	

	// NOT a good idea: (Will cause recursive loop!)
	// this->detachSubObjects()
	// (Yes, include this comment in the code!)
}

void klsCollisionObject::setBBoxChanged() {
	cData.bboxChanged = true;
}

void klsCollisionObject::setBBoxUpdated() {
	cData.bboxChanged = false;
}

bool klsCollisionObject::bboxHasChanged() {
	return cData.bboxChanged;
}

void klsCollisionObject::clearOverlaps() {
	cData.overlaps.clear();
}

void klsCollisionObject::clearSubsOverlaps() {
	CollisionGroup::iterator sub = cData.subObjs.begin();
	while (sub != cData.subObjs.end()) {
		(*sub)->clearOverlaps();
		sub++;
	}
}

void klsCollisionObject::addOverlap(klsCollisionObject* newOverlap) {
	cData.overlaps.insert(newOverlap);
}

void klsCollisionObject::removeOverlap(klsCollisionObject* oldOverlap) {
	cData.overlaps.erase(oldOverlap);
}

CollisionGroup klsCollisionObject::verifyOverlaps() {
	CollisionGroup badOverlaps; // The overlaps that will need removed.
	CollisionGroup::iterator thisOver = cData.overlaps.begin();
	while (thisOver != cData.overlaps.end()) {
		// If the bounding boxes of the collision objects overlap,
		// then we have a collision:
		if (this->overlaps(*thisOver)) {
			// This one's good, so keep checking.
		}
		else {
			// We've got a changed collision, so remember to remove it later:
			badOverlaps.insert(*thisOver);
		}
		thisOver++;
	}

	// Remove all the bad overlaps from the objects:
	thisOver = badOverlaps.begin();
	while (thisOver != badOverlaps.end()) {
		this->removeOverlap(*thisOver);
		(*thisOver)->removeOverlap(this);
		thisOver++;
	}

	return badOverlaps;
}

// ************************* klsCollisionChecker *****************************

// Check the overlaps of all of the collision objects stored in this checker,
// and update their status:
void klsCollisionChecker::update( void ) {
	// Only objects whose bbox changed since the last update are rechecked (call
	// this S), and each is tested not against everything but against the objects
	// sharing its grid cells via the UniformGrid broad phase (see top of file).
	// So the cost is roughly S * (local density), not the old all-against-all N^2.

	// Clear out the old collisions:
	overlaps.clear();

	// Loop through all collision objects, and identify those that have changed:
	CollisionGroup changedObjs, stationaryObjs;
	CollisionGroup::iterator thisObj = collisionObjects.begin();
	while( thisObj != collisionObjects.end() ) {
		// Changed objects and special-type objects (view box, sel box, mouse, etc)
		if( (*thisObj)->bboxHasChanged() || (*thisObj)->getType() > COLL_WIRE_SEG) {
			// Add it to the update list.
			changedObjs.insert( *thisObj );

			// Verify and remove invalid collisions with all "colliding" objects,
			// to remove overlaps that are no longer current:
			(*thisObj)->verifyOverlaps();

			// Tell it that we've fixed the problem:
			(*thisObj)->setBBoxUpdated();
		} else {
			stationaryObjs.insert( *thisObj );
		}
		
		// Add all of the overlaps of this object into the main overlaps object:
		CollisionGroup hits = (*thisObj)->getOverlaps();
		CollisionGroup::iterator thisHit = hits.begin();
		while( thisHit != hits.end() ) {
			overlaps[(*thisHit)->getType()].insert(*thisHit);
			thisHit++;
		}
		
		thisObj++;
	}

	// Now, the question is, which group has more objects?
	CollisionGroup* relChanged = &(changedObjs.size() < stationaryObjs.size() ? changedObjs : stationaryObjs);
	CollisionGroup* relStatic = &(changedObjs.size() >= stationaryObjs.size() ? changedObjs : stationaryObjs);

	// Index the larger group in a spatial grid once, then only test each object
	// of the smaller group against the grid candidates near it. The grid returns
	// a superset of the true overlaps, and checkGroupCollisions still runs the
	// exact same bbox test, so the reported collisions match the old all-against-
	// all scan -- this just skips pairs that are too far apart to touch.
	UniformGrid grid(GRID_CELL_SIZE);
	for (klsCollisionObject *obj : *relStatic)
		grid.insert(obj);

	CollisionGroup::iterator changedObj = relChanged->begin();
	while( changedObj != relChanged->end() ) {
		// Gather only the static objects sharing a cell with this one:
		CollisionGroup groupA, groupB;
		groupA.insert( *changedObj );
		grid.query( (*changedObj)->getBBox(), groupB );
		groupB.erase( *changedObj );

		// Check the collisions of object A with the rest of the group,
		// while not resetting the other object's overlap information:
		CollisionGroup hits;
		hits = checkGroupCollisions( groupA, groupB, false );

		// Sort the hits into the main map object:
		CollisionGroup::iterator thisHit = hits.begin();
		while( thisHit != hits.end() ) {
			overlaps[(*thisHit)->getType()].insert(*thisHit);
			thisHit++;
		}

		// Check the next changed object:
		changedObj++;
	}
}

// Check a specific overlap group against another:
// (NOTE: These groups cannot have a same collision object in both. Their
// intersection must be the null set.)
// If resetOverlaps is true, then it resets the overlaps of all the gates before
// adding new collisions. Otherwise, it will simply add them to the current group.
// (Returns a list of all objects involved in any collisions.)
CollisionGroup klsCollisionChecker::checkGroupCollisions( CollisionGroup groupA, CollisionGroup groupB, bool resetOverlaps ) {
	CollisionGroup collidedObjects;
	CollisionGroup::iterator iterA, iterB;
	
	// Clear out old overlap information if requested:
	if( resetOverlaps ) {
		iterA = groupA.begin();
		while( iterA != groupA.end() ) {
			(*iterA)->clearOverlaps();
			iterA++;
		}
		
		iterB = groupB.begin();
		while( iterB != groupB.end() ) {
			(*iterB)->clearOverlaps();
			iterB++;
		}
	}
	
	// Check each bbox of groupA against all of groupB:
	iterA = groupA.begin();
	while( iterA != groupA.end() ) {
		iterB = groupB.begin();
		while( iterB != groupB.end() ) {

			// If the bounding boxes of the collision objects overlap,
			// then we have a collision:
			if( (*iterA)->overlaps(*iterB) ) {

				// Register the collision in both object's data structures:
				(*iterA)->addOverlap( *iterB );
				(*iterB)->addOverlap( *iterA );

				// Save the collided objects for returning them to the caller:
				collidedObjects.insert( *iterA );
//				collidedObjects.insert( *iterB );
			}

			iterB++;
		}
		iterA++;
	}

	return collidedObjects;
}

void klsCollisionChecker::addObject(klsCollisionObject* newObj) {
	collisionObjects.insert(newObj);
	newObj->setBBoxChanged();
	newObj->clearOverlaps();
	newObj->clearSubsOverlaps();
};

void klsCollisionChecker::removeObject( klsCollisionObject* oldObj ) {
	collisionObjects.erase( oldObj );
	oldObj->detachSubObjects();
	oldObj->detachFromCollisions();
}

void klsCollisionChecker::clear() {
	collisionObjects.clear(); update();
};