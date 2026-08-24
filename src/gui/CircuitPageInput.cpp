/*****************************************************************************
   Project: CEDAR Logic Simulator

   CircuitPage input: what a click selects, what starts a drag, where a wire
   snaps -- the feel of the application, with no toolkit attached.
*****************************************************************************/

// This is the half of the old GUICanvas that was never really about a window.
// It reads toolkit-neutral events (InputEvent.h) and asks its PageHost for the
// few things it cannot know: where the pointer is, when to repaint, how to open
// a picker. Both shells run this same code, which is what makes the browser
// feel like the desktop rather than merely resemble it.

#include "CircuitPage.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "GUICircuit.h"
#include "GateLibrary.h"
#include "PaletteDrag.h"
#include "Settings.h"
#include "commands.h"
#include "guiGate.h"
#include "guiWire.h"
#include "klsClipboard.h"
#include "render/RenderStyle.h"
#include "render/Scene.h"

using namespace std;

// Clears the circuit by selecting all gates and wires and then running a delete command
void CircuitPage::clearCircuit() {
	selectedGates.clear();
	selectedWires.clear();
	preMove.clear();
	preMoveWire.clear();

	clearPage();

	hotspotHighlight = "";
	potentialConnectionHotspots.clear();
	drawWireHover = false;
	isWithinPaste = false;
	saveMove = false;
}

// Tag a command with this canvas, then submit it, so undo/redo can return to
// the page the edit happened on.
// A gate is leaving the page: drop the hover highlight if it was pointing at it.
void CircuitPage::onGateRemoved(unsigned long id) {
	if (hotspotGate == id) hotspotHighlight = "";
}

void CircuitPage::submitCommand(klsCommand *cmd) {
	cmd->setCanvas(this);
	gCircuit->GetCommandProcessor()->Submit((wxCommand *)cmd);
}

void CircuitPage::drawOverlaysInto(cl::render::Scene& scene) {
	using cl::render::Point;
	using cl::render::Color;
	using cl::render::Stroke;
	const float r = HOTSPOT_SCREEN_RADIUS * (float)getCamera()->getZoom();

	auto box = [&scene](float x, float y, float rad, const Color& c) {
		Point pts[4] = { Point(x - rad, y + rad), Point(x + rad, y + rad),
		                 Point(x + rad, y - rad), Point(x - rad, y - rad) };
		scene.polyline(pts, 4, Stroke(c, 1.0f), true);
	};

	// Hovered gate pin -- the red bulb you drag a wire out from.
	if (hotspotHighlight.size() > 0 && gateList.count(hotspotGate) && gateList[hotspotGate]) {
		float x, y;
		gateList[hotspotGate]->getHotspotCoords(hotspotHighlight, x, y);
		box(x, y, r, Color(1.0f, 0.0f, 0.0f));
	}

	// Wire hover -- a red X at the mouse. Only while the hovered wire still
	// exists, so deleting it clears the X on the delete's own repaint instead of
	// leaving it stuck until the next mouse move.
	if (drawWireHover && wireList.count(wireHoverID) && wireList[wireHoverID]) {
		GLPoint2f m = host()->pointerPos();
		Point xs[4] = { Point(m.x - r, m.y + r), Point(m.x + r, m.y - r),
		                Point(m.x + r, m.y + r), Point(m.x - r, m.y - r) };
		scene.lines(xs, 4, Stroke(Color(1.0f, 0.0f, 0.0f), 1.0f));
	}

	if (currentDragState == DRAG_SELECT) {
		GLPoint2f s = host()->dragStart(input::Button::Left), e = host()->pointerPos();
		Point pts[4] = { Point(s.x, s.y), Point(s.x, e.y), Point(e.x, e.y), Point(e.x, s.y) };
		scene.polyline(pts, 4, Stroke(Color(0.0f, 0.4f, 1.0f, 1.0f), 1.0f), true);
		scene.fillRect(Point(s.x, s.y), Point(e.x, e.y), Color(0.0f, 0.4f, 1.0f, 0.3f));
	} else if (currentDragState == DRAG_CONNECT) {
		// Anchor the preview line at the source pin, not the click point.
		GLPoint2f s = host()->dragStart(input::Button::Left);
		if (currentConnectionSource.isGate && gateList.count(currentConnectionSource.objectID) &&
		    gateList[currentConnectionSource.objectID])
			gateList[currentConnectionSource.objectID]->getHotspotCoords(
				currentConnectionSource.connection, s.x, s.y);
		GLPoint2f e = host()->pointerPos();
		Point ln[2] = { Point(s.x, s.y), Point(e.x, e.y) };
		scene.lines(ln, 2, Stroke(Color(0.0f, 0.78f, 0.0f, 1.0f), 1.0f));
	} else if (currentDragState == DRAG_NEWGATE && newDragGate != NULL) {
		newDragGate->drawToScene(scene, cl::render::RenderStyle::screen());
	}

	// Potential connection hotspots -- where a dragged wire could snap.
	for (size_t i = 0; i < potentialConnectionHotspots.size(); i++)
		box(potentialConnectionHotspots[i].x, potentialConnectionHotspots[i].y, r,
		    Color(0.3f, 0.3f, 1.0f));

	// Collision overlaps -- translucent boxes where two gates overlap.
	for (std::map<klsCollisionObjectType, CollisionGroup>::iterator ov = collisionChecker.overlaps.begin();
	     ov != collisionChecker.overlaps.end(); ++ov) {
		if (ov->first != COLL_GATE) continue;
		for (CollisionGroup::iterator obj = ov->second.begin(); obj != ov->second.end(); ++obj) {
			if ((*obj)->getType() != COLL_GATE) continue;
			CollisionGroup hits = (*obj)->getOverlaps();
			for (CollisionGroup::iterator h = hits.begin(); h != hits.end(); ++h) {
				if ((*h)->getType() != COLL_GATE) continue;
				klsBBox hb = (*obj)->getBBox().intersect((*h)->getBBox());
				if (!hb.empty())
					scene.fillRect(Point(hb.getLeft(), hb.getBottom()),
					               Point(hb.getRight(), hb.getTop()),
					               Color(0.4f, 0.1f, 0.0f, 0.3f));
			}
		}
	}
}

// A press routes by button; the two halves diverge immediately.
void CircuitPage::OnMouseDown(const input::PointerEvent& event) {
	if (event.button == input::Button::Left) {
		mouseLeftDown(event);
	} else if (event.button == input::Button::Right) {
		mouseRightDown(event);
	}
}

void CircuitPage::mouseLeftDown(const input::PointerEvent& event) {
	GLPoint2f m = host()->pointerPos();
	bool handled = false;
	dragPressTime = std::chrono::steady_clock::now(); // for the click-vs-drag time dead zone
	// If placing a new gate (snap-to-cursor), let OnMouseUp finalize it
	if (currentDragState == DRAG_NEWGATE) return;
	// If I am in a paste operation then mouse-up is all I am concerned with
	if (isWithinPaste) return;
	
	// Update the mouse collision object
	klsBBox mBox;
	float delta = MOUSE_HOVER_DELTA * getCamera()->getZoom();
	mBox.addPoint( m );
	mBox.extendTop( delta );
	mBox.extendBottom( delta );
	mBox.extendLeft( delta );
	mBox.extendRight( delta );
	mouse->setBBox( mBox );
	
	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	collisionChecker.update();

	// Loop through all objects hit by the mouse
	//	Favor wires over gates
	CollisionGroup hitThings = mouse->getOverlaps();
	CollisionGroup::iterator hit = hitThings.begin();
	while( hit != hitThings.end() && !handled ) {
		//*************************************
		//Edit by Joshua Lansford 3/16/07
		//It has been requested by students that a ctrl
		//click will select multiple gates just like
		//a shift click does.
		//thus I will replace "event.mods.shift"
		//everywere it appears in this file with
		//"event.extendSelection()"
		//************************************
		
		if ((*hit)->getType() == COLL_WIRE) {
			guiWire* hitWire = ((guiWire*)(*hit));
			bool wasSelected = hitWire->isSelected();
			hitWire->unselect();
			if ( hitWire->hover( m.x, m.y, WIRE_HOVER_SCREEN_DELTA * getCamera()->getZoom() )) {
				hitWire->select();
				if (event.extendSelection() && wasSelected) hitWire->unselect();
				if (!(event.extendSelection())) {
					unselectAllWires();
					unselectAllGates();
					hitWire->select();
				}
				if (event.mods.ctrl && !(host()->isLocked())) {
					currentConnectionSource.isGate = false;
					currentConnectionSource.objectID = hitWire->getID();
					currentDragState = DRAG_CONNECT;
				}
				else if (!(event.extendSelection())) {
					wireHoverID = hitWire->getID();
					if (wireList[wireHoverID]->startSegDrag(snapMouse) && !(host()->isLocked())) currentDragState = DRAG_WIRESEG;
					hitWire->unselect();
				}
				handled = true;	
			} else if (wasSelected && hitThings.size() > 1) hitWire->select(); // probably dragging a selection
		}
		hit++;
	}

	// do we have a highlighted hotspot (which means we're on it now)
	if (hotspotHighlight.size() > 0 && currentDragState == DRAG_NONE && !(host()->isLocked())) {
		// Start dragging a new wire:
		//gateList[hotspotGate]->select();
		unselectAllGates();
		unselectAllWires();
		handled = true; // Don't worry about checking other events in this proc
		currentDragState = DRAG_CONNECT;
		currentConnectionSource.isGate = true;
		currentConnectionSource.objectID = hotspotGate;
		currentConnectionSource.connection = hotspotHighlight;
	}

	// Now check gate collisions
	hit = hitThings.begin();
	while( hit != hitThings.end() && !handled ) {
		if ((*hit)->getType() == COLL_GATE) {
			guiGate* hitGate = ((guiGate*)(*hit));
			// The gate is in hitThings via its collision box, which is grown to
			// enclose its hotspot pins -- so clicking the empty space beside a pin
			// lands in that box. Only treat it as a selection if the click is on the
			// gate BODY, so pins stay for wire-connecting, not selecting.
			if (!hitGate->getSelectionBBox().overlaps(mouse->getBBox())) { hit++; continue; }
			bool wasSelected = hitGate->isSelected();
			if (event.extendSelection() && wasSelected) hitGate->unselect(); // Remove gate from selection
			else if (event.extendSelection() && !wasSelected) hitGate->select(); // Add gate to selection
			else if (!(event.extendSelection()) && !wasSelected) { // Begin new selection group
				unselectAllGates();
				unselectAllWires();
				hitGate->select();
			}
			if (!(event.extendSelection()) && !(host()->isLocked())) currentDragState = DRAG_SELECTION; // Start dragging
			handled = true;
		}
		hit++;
	}

	// If I am not in a selection group and I haven't handled a selection then unselect everything
	if (!handled && !(event.extendSelection())) {
		unselectAllGates();
		unselectAllWires();
	}
	
	if (!handled) { // Otherwise initialize drag select
		currentDragState = DRAG_SELECT;
	}
	
	// Show the updates
	host()->requestRepaint();

	// clean up the selected gates vector and saved premove state
	selectedGates.clear();
	preMove.clear();
	saveMove = false;
	unordered_map < unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		if ((thisGate->second)->isSelected()) {
			// Push back the gate's id, xy pos, angle, and select flag
			preMove.push_back(GateState((thisGate->first), 0, 0, (thisGate->second)->isSelected()));
			(thisGate->second)->getGLcoords(preMove[preMove.size()-1].x, preMove[preMove.size()-1].y);
			selectedGates.push_back((thisGate->first));
		}
		thisGate++;
	}
	// clean up the selected wires vector
	selectedWires.clear();
	preMoveWire.clear();
	unordered_map < unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		if (thisWire->second != nullptr) {
			if ((thisWire->second)->isSelected()) {
				// Push back the wire's id
				preMoveWire.push_back(WireState((thisWire->first), (thisWire->second)->getCenter(), (thisWire->second)->getSegmentMap()));
				selectedWires.push_back((thisWire->first));
			}
		}
		thisWire++;
	}
}

void CircuitPage::mouseRightDown(const input::PointerEvent& event) {
	GLPoint2f m = host()->pointerPos();
	vector < unsigned long >::iterator sGate;

	if (isWithinPaste || (currentDragState != DRAG_NONE)) return; // Left mouse up is the next event we are looking for

	// Update the mouse collision object
	klsBBox mBox;
	float delta = MOUSE_HOVER_DELTA * getCamera()->getZoom();
	mBox.addPoint( m );
	mBox.extendTop( delta );
	mBox.extendBottom( delta );
	mBox.extendLeft( delta );
	mBox.extendRight( delta );
	mouse->setBBox( mBox );
	
	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	collisionChecker.update();

	// Go ahead and remove all selection since this is the right mouse button
	unselectAllGates();
	unselectAllWires();

	// If locked then we have nothing else to do
	if ( host()->isLocked() ) return;

	// do we have a highlighted hotspot (which means we're on it now)
	if (hotspotHighlight.size() > 0) {
		// If the hotspot is connected then we disconnect it and generate a command
		if (gateList[hotspotGate]->isConnected(hotspotHighlight)) {
			// disconnect this wire
			if (gateList[hotspotGate]->getConnection(hotspotHighlight)->numConnections() > 2)
				submitCommand( new cmdDisconnectWire( gCircuit, gateList[hotspotGate]->getConnection(hotspotHighlight)->getID(), hotspotGate, hotspotHighlight ) );
			else submitCommand( new cmdDeleteWire( gCircuit, this, gateList[hotspotGate]->getConnection(hotspotHighlight)->getID() ) );
		}
		currentDragState = DRAG_NONE;
	} else if (currentDragState == DRAG_NONE && appConfig().appSettings.rightClickRotate) {
		// Not on a hotspot, so check if it's on a gate:
		// Loop through all objects hit by the mouse
		CollisionGroup hitThings = mouse->getOverlaps();
		CollisionGroup::iterator hit = hitThings.begin();
		unselectAllGates();
		unselectAllWires();
		while( hit != hitThings.end()) {
			if ((*hit)->getType() == COLL_GATE) {
				guiGate* hitGate = ((guiGate*)(*hit));
				// BEGIN WORKAROUND
				//	Gates that have connections cannot be rotated without sacrificing wire sanity
				map < string, GLPoint2f > gateHotspots = hitGate->getHotspotList();
				map < string, GLPoint2f >::iterator ghsWalk = gateHotspots.begin();
				bool gateConnected = false;
				while ( ghsWalk !=  gateHotspots.end() ) {
					if ( hitGate->isConnected( ghsWalk->first ) ) {
						gateConnected = true;
						break;
					}
					ghsWalk++;
				}
				if ( gateConnected ) { hit++; continue; }
				// END WORKAROUND
				map < string, string > newParams(*(hitGate->getAllGUIParams()));
				istringstream issAngle(newParams["angle"]);
				GLfloat angle;
				issAngle >> angle;
				angle += 90.0;
				if (angle >= 360.0) angle -= 360.0;
				ostringstream ossAngle;
				ossAngle << angle;
				newParams["angle"] = ossAngle.str();
				submitCommand( new cmdSetParams(gCircuit, hitGate->getID(), paramSet(&newParams, NULL) ) );
			}
			hit++;
		}				
	}
	host()->requestRepaint();
}

void CircuitPage::OnMouseMove(const input::PointerEvent& event) {
	const GLdouble glX = event.pos.x, glY = event.pos.y;
	const bool ShiftDown = event.mods.shift, CtrlDown = event.mods.ctrl;
	// Handle gate dragging from palette (especially needed for macOS where OnMouseEnter
	// may not fire correctly when mouse is captured)
	if (paletteDrag().newGateToDrag.size() > 0 && currentDragState == DRAG_NONE && !(host()->isLocked())) {
		GLPoint2f m = host()->pointerPos();
		newDragGate = gCircuit->createGate(paletteDrag().newGateToDrag, -1);
		if (newDragGate != NULL) {
			newDragGate->setGLcoords(m.x, m.y);
			currentDragState = DRAG_NEWGATE;
			paletteDrag().newGateToDrag = "";
			host()->beginDrag(input::Button::Left);
			unselectAllGates();
			newDragGate->select();
			collisionChecker.addObject( newDragGate );
		} else {
			paletteDrag().newGateToDrag = "";
		}
	}

	// Keep a flag for whether things have changed.  If nothing changes, then no render is necessary.
	bool shouldRender = false;

	GLPoint2f m = host()->pointerPos();
	GLPoint2f dStart = host()->dragStart(input::Button::Left);
	GLPoint2f diff( m.x - dStart.x, m.y - dStart.y ); // What is the difference between start and now

	GLPoint2f mSnap = getCamera()->getSnappedPoint( m ); // Work with a snapped mouse coord
	GLPoint2f dStartSnap = getCamera()->getSnappedPoint( dStart );
	GLPoint2f diffSnap( mSnap.x - dStartSnap.x, mSnap.y - dStartSnap.y );

	// Click-vs-drag dead zone: treat this as a click (no move) while the pointer
	// is still within a small pixel radius of the press point, OR within a short
	// time of it -- so neither a jittery nor a quick-but-traveling selection click
	// nudges the gate into the next grid cell.
	{
		float dead = DRAG_START_SCREEN_DELTA * getCamera()->getZoom();
		float ax = diff.x < 0 ? -diff.x : diff.x;
		float ay = diff.y < 0 ? -diff.y : diff.y;
		long long heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - dragPressTime).count();
		if ((ax < dead && ay < dead) || heldMs < DRAG_START_TIME_MS) {
			diffSnap.x = 0.0f; diffSnap.y = 0.0f;
		}
	}

	// Update the mouse as a collision object:
	klsBBox mBox;
	float delta = MOUSE_HOVER_DELTA * getCamera()->getZoom();
	mBox.addPoint( m );
	mBox.extendTop( delta );
	mBox.extendBottom( delta );
	mBox.extendLeft( delta );
	mBox.extendRight( delta );
	mouse->setBBox( mBox );

	klsBBox smBox;
	smBox.addPoint( mSnap );
	snapMouse->setBBox( smBox );

	// Update the drag select box coordinates:
	klsBBox dBox;
	dBox.addPoint( dStart );
	dBox.addPoint( m );
	dragselectbox->setBBox( dBox );
	
	if ( host()->isLocked() ) return;

	// Hover-work throttle. OnMouseMove fires once per raw motion event -- hundreds
	// per second during a fast sweep -- but the collision pass + hover highlight
	// only need to refresh ~60x/sec. When not in a drag, skip the heavy work if it
	// ran <15ms ago; this is what stops fast mouse movement from saturating the
	// GUI thread and starving the sim's timers (the clock/oscope would visibly
	// lag). Clicks stay accurate: mouseLeftDown runs its own collision update.
	// Drags are never throttled -- they must track the cursor exactly.
	if ( currentDragState == DRAG_NONE ) {
		auto now = std::chrono::steady_clock::now();
		if ( std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHoverTime).count() < 15 )
			return;
		lastHoverTime = now;
	}

	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	collisionChecker.update();

	// Update a newly-dragged gate's position
	if (currentDragState == DRAG_NEWGATE) {
		shouldRender = true;
		newDragGate->setGLcoords(mSnap.x, mSnap.y);
	}
	
	// If the hotspot hover is on, make it clear
	if (hotspotHighlight.size() > 0) shouldRender = true;
	hotspotHighlight = "";

	// If necessary, save the move being done.
	if (preMove.size() > 0 && currentDragState != DRAG_CONNECT && (diffSnap.x != 0 || diffSnap.y != 0)) saveMove = true;

	if (currentDragState == DRAG_SELECTION) {
		// Move all gates that are selected in the preMove vector:
		for (unsigned int i = 0; i < preMoveWire.size(); i++) wireList[preMoveWire[i].id]->move(preMoveWire[i].point, diffSnap);
		for (unsigned int i = 0; i < preMove.size(); i++) gateList[preMove[i].id]->setGLcoords(preMove[i].x+diffSnap.x, preMove[i].y+diffSnap.y);
	} else if (currentDragState == DRAG_WIRESEG) {
		wireList[wireHoverID]->updateSegDrag(snapMouse);
	}

	// Generate a new list of potential connections
	potentialConnectionHotspots.clear();
	// Reset wire hover flag
	if (drawWireHover) shouldRender = true;
	drawWireHover = false;

	CollisionGroup hitThings = mouse->getOverlaps();
	CollisionGroup::iterator hit = hitThings.begin();
	while( hit != hitThings.end()) {
		if ((*hit)->getType() == COLL_GATE) {
			guiGate* hitGate = ((guiGate*)(*hit));
			
			// Update the hotspot hover variables:
			if( hotspotHighlight.size() == 0 ) {
				if (currentDragState != DRAG_NEWGATE || hitGate->getID() != newDragGate->getID()) hotspotHighlight = hitGate->checkHotspots( m.x, m.y, HOTSPOT_SCREEN_DELTA * getCamera()->getZoom() );
				if( hotspotHighlight.size() > 0 ) {
					if (currentDragState != DRAG_NEWGATE || hitGate->getID() != newDragGate->getID()) hotspotGate = hitGate->getID();
					shouldRender = true;
				}
			}
			
		}
		if ((*hit)->getType() == COLL_WIRE && !drawWireHover && currentDragState != DRAG_WIRESEG) { // Check for wire hover
			guiWire* hitWire = ((guiWire*)(*hit));
			drawWireHover = hitWire->hover( m.x, m.y, WIRE_HOVER_SCREEN_DELTA * getCamera()->getZoom() );
			wireHoverID = hitWire->getID();
			if (drawWireHover) shouldRender = true;
		}
		hit++;
	}

	if (currentDragState == DRAG_SELECT) { // Check for items within drag select box
		unselectAllGates();
		unselectAllWires();
		// Other items may have been selected before if we're using shift/control
		for (unsigned int i = 0; i < preMove.size(); i++) {
			gateList[preMove[i].id]->select();
		}
		for (unsigned int i = 0; i < preMoveWire.size(); i++) {
			wireList[preMoveWire[i].id]->select();
		}
		// Now check the collision box for dragselects
		CollisionGroup selThings = dragselectbox->getOverlaps();
		hit = selThings.begin();
		while( hit != selThings.end()) {
			if ((*hit)->getType() == COLL_GATE) {
				guiGate* hitGate = ((guiGate*)(*hit));
				if (dBox.contains((*hit)->getBBox())) hitGate->select();
			}
			if ((*hit)->getType() == COLL_WIRE) {
				guiWire* hitWire = ((guiWire*)(*hit));
				if (dBox.contains((*hit)->getBBox())) hitWire->select();
			}
			hit++;
		}
	}
	
	// Check potential hotspot connections (on gate/gate collisions)
	CollisionGroup ovrList = collisionChecker.overlaps[COLL_GATE];
	CollisionGroup::iterator obj = ovrList.begin();
	while( obj != ovrList.end() ) {
		CollisionGroup hitThings = (*obj)->getOverlaps();
		CollisionGroup::iterator hit = hitThings.begin();
		while( hit != hitThings.end() ) {
			// Only check gate collisions
			if ((*hit)->getType() != COLL_GATE) { hit++; continue; };
			// obj and hit are two overlapping gates
			//  get overlapping hotspots of obj in another group
			CollisionGroup hotspotOverlaps = (*obj)->checkSubsToSubs(*hit);
			CollisionGroup::iterator hotspotCollide = hotspotOverlaps.begin();
			while (hotspotCollide != hotspotOverlaps.end()) {
				// hotspotCollide is in obj; hsWalk is in hit
				CollisionGroup hshits = (*hotspotCollide)->getOverlaps();
				CollisionGroup::iterator hsWalk = hshits.begin();
				while (hsWalk != hshits.end()) {
					if ( !(((guiGate*)(*obj))->isConnected(((gateHotspot*)(*hotspotCollide))->name)) && !(((guiGate*)(*hit))->isConnected(((gateHotspot*)(*hsWalk))->name)))
						potentialConnectionHotspots.push_back( ((gateHotspot*)(*hotspotCollide))->getLocation() );
					hsWalk++;
				}
				hotspotCollide++;
			}
			hit++;
		}
		obj++;
	}

	if (currentDragState == DRAG_SELECTION || currentDragState == DRAG_SELECT || currentDragState == DRAG_CONNECT || currentDragState == DRAG_WIRESEG) shouldRender = true;
	
	// Only render if necessary
	if (shouldRender) {
		host()->requestRepaint();
	}
	
	// clean up the selected gates vector
	selectedGates.clear();
	unordered_map < unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		if ((thisGate->second)->isSelected()) selectedGates.push_back((thisGate->first));
		thisGate++;
	}
	// clean up the selected wires vector
	selectedWires.clear();
	unordered_map < unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		if (thisWire->second != nullptr) {
			if ((thisWire->second)->isSelected()) selectedWires.push_back((thisWire->first));
		}
		thisWire++;
	}
}

void CircuitPage::OnMouseUp(const input::PointerEvent& event) {
	GLPoint2f m = host()->pointerPos();
	host()->setArrowCursor();
	unordered_map < unsigned long, guiGate* >::iterator thisGate;
	cmdMoveSelection* movecommand = NULL;
	cmdCreateGate* creategatecommand = NULL;

	// Update the drag select box coordinates for wire source detection:
	klsBBox dBox;
	float delta = HOTSPOT_SCREEN_DELTA * getCamera()->getZoom();
	dBox.addPoint( host()->dragStart(input::Button::Left) );
	dBox.extendTop( delta );
	dBox.extendBottom( delta );
	dBox.extendLeft( delta );
	dBox.extendRight( delta );
	dragselectbox->setBBox( dBox );

	// If moving a selection then save the move as a command
	if (saveMove && currentDragState == DRAG_SELECTION) {
		float gX, gY;
		if (preMove.size() > 0) {
			gateList[preMove[0].id]->getGLcoords(gX, gY);
			movecommand = new cmdMoveSelection( gCircuit, preMove, preMoveWire, preMove[0].x, preMove[0].y, gX, gY );
			for (unsigned int i = 0; i < preMove.size(); i++) gateList[preMove[i].id]->updateConnectionMerges();
			if (!isWithinPaste) submitCommand( movecommand );
			if (!isWithinPaste) movecommand->Undo();
		}
		if (preMove.size() > 1) preMove.clear();
	}

	// Check for single selection out of group
	if (preMove.size() > 0) {
		float gX, gY;
		gateList[preMove[0].id]->getGLcoords(gX, gY);
		if (gX == preMove[0].x && gY == preMove[0].y && !(event.extendSelection())) { // no move
			CollisionGroup hitThings = mouse->getOverlaps();
			CollisionGroup::iterator hit = hitThings.begin();
			while( hit != hitThings.end() ) {
				if ((*hit)->getType() == COLL_GATE) {
					guiGate* hitGate = ((guiGate*)(*hit));
					unselectAllGates();
					unselectAllWires();
					preMove.clear();
					preMove.push_back(GateState(hitGate->getID(), 0, 0, true));
					hitGate->select();
					saveMove = false;
					break;
				}
				hit++;
			}		
		}
	}

	if (currentDragState == DRAG_WIRESEG) {
		wireList[wireHoverID]->endSegDrag();
		wireList[wireHoverID]->select();
		submitCommand( new cmdWireSegDrag( gCircuit, this, wireHoverID ) );
	}

	// If dragging a new gate then 
	if (currentDragState == DRAG_NEWGATE) {
		int newGID = gCircuit->getNextAvailableGateID();
		float nx, ny;
		newDragGate->getGLcoords(nx, ny);
		gCircuit->getGates()->erase(newDragGate->getID());
		creategatecommand = new cmdCreateGate( this, gCircuit, newGID, newDragGate->getLibraryGateName(), nx, ny );
		gCircuit->GetCommandProcessor()->Submit( (wxCommand*)creategatecommand );
		collisionChecker.removeObject( newDragGate );
		// Only now do a collision detection on all first-level objects since the new gate is in.
		// The map collisionChecker.overlaps now contains
		// all of the objects involved in any collisions.
		collisionChecker.update();
		cmdSetParams setgateparams( gCircuit, newGID, paramSet((*(gCircuit->getGates()))[newGID]->getAllGUIParams(), (*(gCircuit->getGates()))[newGID]->getAllLogicParams()));
		setgateparams.Do();
		delete newDragGate;
		gateList[newGID]->select();
		selectedGates.push_back(newGID);
	}
	else {
		// Do a collision detection on all first-level objects.
		// The map collisionChecker.overlaps now contains
		// all of the objects involved in any collisions.
		collisionChecker.update();
		
		if ((currentDragState == DRAG_NONE || currentDragState == DRAG_SELECTION) && preMove.size() == 1) {
			// Loop through all objects hit by the mouse
			//	Favor wires over gates
//			CollisionGroup hitThings = mouse->getOverlaps();
//			CollisionGroup::iterator hit = hitThings.begin();
//			while( hit != hitThings.end() && !handled ) {
//				if ((*hit)->getType() == COLL_GATE) {
//					guiGate* hitGate = ((guiGate*)(*hit));
					// Check that gate still exists (may have been deleted by undo)
					if (gateList.find(preMove[0].id) != gateList.end()) {
						guiGate* hitGate = gateList[preMove[0].id];
						if (!(event.extendSelection()) && (((event.button == input::Button::Left) && currentDragState == DRAG_SELECTION) || event.leftDoubleClick())) {
							// Check for toggle switch
							float x, y;
							hitGate->getGLcoords(x,y);
							bool handled = false;
							if (!saveMove) {
								klsMessage::Message_SET_GATE_PARAM* clickHandleGate = hitGate->checkClick( m.x, m.y );
								if (clickHandleGate != NULL) {
									gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, clickHandleGate));
									handled = true;
								}
							}
							if (event.leftDoubleClick() && !handled) {
								hitGate->doParamsDialog( gCircuit, gCircuit->GetCommandProcessor() );
								currentDragState = DRAG_NONE;
								// setparams command will handle oscope update
								handled = true;
							}
						}
					}
//				}
//				hit++;
//			}
		}

		// If we are dragging something...
		if (currentDragState == DRAG_CONNECT) {

			// Oh yeah, we only drag from gates...
			if (currentConnectionSource.isGate) {

				wxCommand *command = nullptr;

				// Target is a wire...
				if (drawWireHover) {
					command = createGateWireConnectionCommand(
						currentConnectionSource.objectID,
						currentConnectionSource.connection, wireHoverID);
				}
				else {

					// Target is a gate...
					if (hotspotHighlight.size() > 0) {
						command = createGateConnectionCommand(
							currentConnectionSource.objectID,
							currentConnectionSource.connection,
							hotspotGate, hotspotHighlight);
					}
				}

				if (command != nullptr) {
					submitCommand((klsCommand *)command);
				}
			}
		}

		collisionChecker.update();
	}

	if ((currentDragState == DRAG_NEWGATE || currentDragState == DRAG_SELECTION) && (potentialConnectionHotspots.size() > 0)) {
		// Check potential hotspot connections (on gate/gate collisions)
		CollisionGroup ovrList = collisionChecker.overlaps[COLL_GATE];
		CollisionGroup::iterator obj = ovrList.begin();
		while( obj != ovrList.end() ) {
			CollisionGroup hitThings = (*obj)->getOverlaps();
			CollisionGroup::iterator hit = hitThings.begin();
			while( hit != hitThings.end() ) {
				// Only check gate collisions
				if ((*hit)->getType() != COLL_GATE) { hit++; continue; };
				// obj and hit are two overlapping gates
				//  get overlapping hotspots of obj in another group
				CollisionGroup hotspotOverlaps = (*obj)->checkSubsToSubs(*hit);
				CollisionGroup::iterator hotspotCollide = hotspotOverlaps.begin();
				while (hotspotCollide != hotspotOverlaps.end()) {
					// hotspotCollide is in obj; hsWalk is in hit
					CollisionGroup hshits = (*hotspotCollide)->getOverlaps();
					CollisionGroup::iterator hsWalk = hshits.begin();
					while (hsWalk != hshits.end()) {
						if (!(((guiGate*)(*obj))->isConnected(((gateHotspot*)(*hotspotCollide))->name)) && !(((guiGate*)(*hit))->isConnected(((gateHotspot*)(*hsWalk))->name)) &&
							(((guiGate*)(*obj))->isSelected() || ((guiGate*)(*hit))->isSelected())) {

							cmdCreateWire* createwire = (cmdCreateWire *)createGateConnectionCommand(
								((guiGate*)(*obj))->getID(), ((gateHotspot*)(*hotspotCollide))->name,
								((guiGate*)(*hit))->getID(), ((gateHotspot*)(*hsWalk))->name);

							if (createwire != nullptr) {
								createwire->Do();
								//collisionChecker.update();
								if (currentDragState == DRAG_SELECTION) {
									if (movecommand == NULL) {
										movecommand = new cmdMoveSelection(gCircuit, preMove, preMoveWire, 0, 0, 0, 0);
										if (!isWithinPaste) submitCommand(movecommand);
									}
									movecommand->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
								}
								else if (currentDragState == DRAG_NEWGATE) creategatecommand->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
								else delete createwire;
							}
						}
						hsWalk++;
					}
					hotspotCollide++;
				}
				hit++;
			}
			obj++;
		}
	}

	// Drop a paste block with the proper move coords
	if (isWithinPaste) {
		pasteCommand->addCommand( movecommand );
		submitCommand( pasteCommand );
		isWithinPaste = false;
		host()->setAutoScroll(true); // Re-enable auto scrolling
	}
	
	currentDragState = DRAG_NONE;
	
	updatePage();
}

// Add a gate from a drag and drop operation.
//
// The way this was originally done relied on mouse events switching from the
// selector pane to the main canvas, which doesn't happen when using gtk.
//
// This is a hacky solution, but it avoids needing to refactor the OnMouseUp
// event.
void CircuitPage::addGate(string gate, GLPoint2f m) {
	newDragGate = gCircuit->createGate(gate, -1);
	if (newDragGate == NULL) return;

	newDragGate->setGLcoords(m.x, m.y);
	currentDragState = DRAG_NEWGATE;

	unselectAllGates();
	newDragGate->select();
	collisionChecker.addObject( newDragGate );

	// Finish the placement through the ordinary release path, so a gate dropped
	// from the palette lands exactly the way a hand-placed one does.
	input::PointerEvent release;
	release.button = input::Button::Left;
	release.pos = host()->pointerPos();
	OnMouseUp(release);
}

void CircuitPage::OnMouseEnter(const input::PointerEvent& event) {
	GLPoint2f m = host()->pointerPos();

	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	//collisionChecker.update();

	paletteDrag().showDragImage = false;
	if (event.leftIsDown && paletteDrag().newGateToDrag.size() > 0 && currentDragState == DRAG_NONE && !(host()->isLocked())) {
		newDragGate = gCircuit->createGate(paletteDrag().newGateToDrag, -1);
		if (newDragGate == NULL) { paletteDrag().newGateToDrag = ""; return; }
		newDragGate->setGLcoords(m.x, m.y);
		currentDragState = DRAG_NEWGATE;
		paletteDrag().newGateToDrag = "";
		host()->beginDrag(input::Button::Left);
		unselectAllGates();
		newDragGate->select();
		collisionChecker.addObject( newDragGate );
	}
	// Don't clear newGateToDrag here — OnMouseMove handles it for
	// both palette drags and quick-add placement.
}

// Cancel any in-progress drag and restore pre-drag state. Invoked by Escape and,
// via the base cancelDrag() hook, on a lost mouse capture (so an OS capture steal
// mid new-gate/paste/move doesn't leave currentDragState + newDragGate orphaned).
void CircuitPage::cancelDrag() {
	unselectAllGates();
	unselectAllWires();
	if (currentDragState == DRAG_NEWGATE && newDragGate != nullptr) {
		gCircuit->getGates()->erase(newDragGate->getID());
		collisionChecker.removeObject( newDragGate );
		delete newDragGate;
		newDragGate = nullptr;
		collisionChecker.update();
		paletteDrag().newGateToDrag = "";
	} else if (isWithinPaste) {
		// Cancel paste operation: undo all pasted gates/wires
		pasteCommand->Undo();
		delete pasteCommand;
		pasteCommand = nullptr;
		isWithinPaste = false;
		preMove.clear();
		preMoveWire.clear();
		collisionChecker.update();
	} else {
		if (preMove.size() > 0) {
			saveMove = false;
			for (unsigned int i = 0; i < preMove.size(); i++) {
				if (gateList.find(preMove[i].id) == gateList.end()) continue;
				gateList[preMove[i].id]->setGLcoords(preMove[i].x, preMove[i].y);
				if (preMove[i].selected) gateList[preMove[i].id]->select();
			}
			preMove.clear();
		}
		if (preMoveWire.size() > 0) {
			for (unsigned int i = 0; i < preMoveWire.size(); i++) {
				if (wireList.find(preMoveWire[i].id) == wireList.end()) continue;
				wireList[preMoveWire[i].id]->setSegmentMap(preMoveWire[i].oldWireTree);
				wireList[preMoveWire[i].id]->select();
			}
		}
	}
	currentDragState = DRAG_NONE;
	host()->endDrag(input::Button::Left);
	host()->requestRepaint();
}

bool CircuitPage::OnKeyDown(const input::KeyEvent& event) {
	switch (event.key) {
	case input::Key::Delete:
	case input::Key::Backspace:  // macOS "delete" key
		if (currentDragState == DRAG_NONE && !(host()->isLocked())) deleteSelection();
		break;
	case input::Key::Escape:
		cancelDrag();
		break;
	case input::Key::Left:
		getCamera()->translatePan(-PAN_STEP * getCamera()->getZoom(), 0.0);
		break;
	case input::Key::Right:
		getCamera()->translatePan(+PAN_STEP * getCamera()->getZoom(), 0.0);
		break;
	case input::Key::Up:
		getCamera()->translatePan(0.0, PAN_STEP * getCamera()->getZoom());
		break;
	case input::Key::Down:
		getCamera()->translatePan(0.0, -PAN_STEP * getCamera()->getZoom());
		break;
	// '=' zooms in as well as '+', so zooming does not need the shift key.
	case input::Key::Plus:
	case input::Key::Equals:
		zoomIn();
		break;
	case input::Key::Minus:
		zoomOut();
		break;
	case input::Key::Space:
		setZoomAll();
		break;
	case input::Key::Character:
		if (event.ch == 'a' || event.ch == 'A') {
			if (event.unmodified() && currentDragState == DRAG_NONE && !host()->isLocked())
				host()->requestQuickAdd();
		} else if (event.ch == 'r' || event.ch == 'R') {
			if (event.unmodified() && !host()->isLocked()) {
				rotateSelection();
				host()->requestRepaint();
			}
		}
		break;
	default:
		break;
	}

	// Always claim the key. This canvas has never let one fall through to
	// klsGLCanvas's own bindings (it never called Skip()), and the two sets
	// overlap -- letting both run would pan twice per arrow press.
	return true;
}

void CircuitPage::deleteSelection() {
	// whatever is in the selected vectors goes
	if (selectedWires.size() > 0 || selectedGates.size() > 0) submitCommand( new cmdDeleteSelection( gCircuit, this, selectedGates, selectedWires ) );
	selectedWires.clear();
	selectedGates.clear();
	preMove.clear();
	saveMove = false;

	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	updatePage();
}

void CircuitPage::unselectAllGates() {
	unordered_map < unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		(thisGate->second)->unselect();
		thisGate++;
	}
}

void CircuitPage::unselectAllWires() {
	unordered_map < unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		if (thisWire->second != nullptr) {
			(thisWire->second)->unselect();
		}
		thisWire++;
	}
}	

void CircuitPage::copyBlockToClipboard () {
	klsClipboard myClipboard;
	// Ship the selected gates and wires out to the clipboard
	myClipboard.copyBlock( gCircuit, this, selectedGates, selectedWires );
}

void CircuitPage::cutSelectionToClipboard () {
	// Copy first -- deleteSelection() clears the selection vectors it reads from.
	copyBlockToClipboard();
	deleteSelection();
}

void CircuitPage::pasteBlockFromClipboard () {
	if (host()->isLocked()) return;
	
	klsClipboard myClipboard;
	pasteCommand = myClipboard.pasteBlock( gCircuit, this );
	if (pasteCommand == NULL) return;
	currentDragState = DRAG_SELECTION; // drag until dropped
	isWithinPaste = true;
	saveMove = true;
	
	// clean up the selected gates vector
	selectedGates.clear();
	preMove.clear();
	unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	unsigned long snapToGateID = 0;
	GLPoint2f gatecoord;
	// paste only to snapped point
	GLPoint2f mc = getCamera()->getSnappedPoint(host()->pointerPos());
	GLPoint2f minPoint;
	bool ref = false;
	// Find top-left-most point
	while (thisGate != gateList.end()) {
		GLPoint2f temp;
		if ((thisGate->second)->isSelected()) {
			(thisGate->second)->getGLcoords(temp.x, temp.y);
			if (temp.x < minPoint.x || !ref) minPoint.x = temp.x;
			if (temp.y > minPoint.y || !ref) minPoint.y = temp.y;
			ref = true;
		}
		thisGate++;
	}
	ref = false;
	// Try to drag by the top-left-most gate
	double minMagnitude = 0.0;
	thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		GLPoint2f temp;
		if ((thisGate->second)->isSelected()) {
			if (ref) {
				(thisGate->second)->getGLcoords(temp.x, temp.y);
				float diffx = gatecoord.x - minPoint.x, diffy = gatecoord.y - minPoint.y;
				double newMag = (diffx * diffx) + (diffy * diffy);
				if (newMag < minMagnitude) {
					minMagnitude = newMag;
					gatecoord = temp;
					snapToGateID = (thisGate->first);
				}
			} else {
				(thisGate->second)->getGLcoords(gatecoord.x, gatecoord.y);
				float diffx = gatecoord.x - minPoint.x, diffy = gatecoord.y - minPoint.y;
				minMagnitude = (diffx * diffx) + (diffy * diffy);
				snapToGateID = (thisGate->first);
				ref = true;
			}
		}
		thisGate++;
	}

	// What is the difference between that gate and the mouse coords
	GLPoint2f diff( mc.x-gatecoord.x, mc.y-gatecoord.y );
	// Shift all the gates and track their differences by command
	thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		if ((thisGate->second)->isSelected()) {
			preMove.push_back(GateState((thisGate->first), 0, 0, (thisGate->second)->isSelected()));
//			(thisGate->second)->translateGLcoords(diff.x, diff.y);
			(thisGate->second)->getGLcoords(preMove[preMove.size()-1].x, preMove[preMove.size()-1].y);
			preMove[preMove.size()-1].x += diff.x; preMove[preMove.size()-1].y += diff.y;
			cmdMoveGate* mgcmd = new cmdMoveGate(gCircuit, (thisGate->first), preMove[preMove.size()-1].x-diff.x, preMove[preMove.size()-1].y-diff.y, preMove[preMove.size()-1].x, preMove[preMove.size()-1].y, true);
			mgcmd->Do();
			pasteCommand->addCommand( mgcmd );
			selectedGates.push_back((thisGate->first));
		}
		thisGate++;
	}
	// clean up the selected wires vector
	selectedWires.clear();
	preMoveWire.clear();
	unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		if (thisWire->second != nullptr) {
			if ((thisWire->second)->isSelected()) {
				// Push back the wire's id and set up a premove state
				cmdMoveWire* movewire = new cmdMoveWire(gCircuit, (thisWire->first), (thisWire->second)->getSegmentMap(), diff);
				movewire->Do();
				pasteCommand->addCommand(movewire);
				preMoveWire.push_back(WireState((thisWire->first), (thisWire->second)->getCenter(), (thisWire->second)->getSegmentMap()));
				selectedWires.push_back((thisWire->first));
			}
		}
		thisWire++;
	} 

	host()->setAutoScroll(false);
	host()->beginDrag(input::Button::Left);
	
	updatePage();
}

// Zoom the canvas to fit all items within it:
void CircuitPage::setZoomAll( void ) {
// TODO: BUG this function sometimes hangs the program.
	klsBBox zoomBox;

	// Add all the gates into the zoom all box:
	unordered_map< unsigned long, guiGate* >::iterator gateWalk = gateList.begin();
	while( gateWalk != gateList.end() ) {
		zoomBox.addBBox( (gateWalk->second)->getBBox() );
		gateWalk++;
	}

	// Add all the wires into the zoom all box:
	unordered_map< unsigned long, guiWire* >::iterator wireWalk = wireList.begin();
	while( wireWalk != wireList.end() ) {
		if (wireWalk->second != nullptr) {
			zoomBox.addBBox((wireWalk->second)->getBBox());
		}
		wireWalk++;
	}
	
	// Make sure to not have a dumb zoom factor on an empty canvas:
	if( gateList.empty() ) {
		zoomBox.addPoint(GLPoint2f(0, 0));
	}
	
	// Put some margin around the zoom box:
	zoomBox.extendTop( ZOOM_ALL_MARGIN );
	zoomBox.extendBottom( ZOOM_ALL_MARGIN );
	zoomBox.extendLeft( ZOOM_ALL_MARGIN );
	zoomBox.extendRight( ZOOM_ALL_MARGIN );

	// Zoom to the zoom-all box:
	getCamera()->setViewport( zoomBox.getTopLeft(), zoomBox.getBottomRight() );
}

// Refresh the collision index, then ask for a repaint. A shell with more to
// keep in step (the desktop's minimap) overrides this and calls up.
void CircuitPage::updatePage() {
	collisionChecker.update();
	if (host()) host()->requestRepaint();
}

void CircuitPage::zoomIn() {
	//Only zoom when not dragging
	if (currentDragState == DRAG_NONE) {
		getCamera()->setZoom(getCamera()->getZoom() * ZOOM_STEP);
	}
}

void CircuitPage::zoomOut() {
	//Only zoom when not dragging
	if (currentDragState == DRAG_NONE) {
		getCamera()->setZoom(getCamera()->getZoom() / ZOOM_STEP);
	}
}

klsCommand * CircuitPage::createGateWireConnectionCommand(IDType gateId, const string &hotspot, IDType wireId) {

	guiGate *gate = gateList[gateId];
	guiWire *wire = wireList[wireId];

	// Make sure not already connected.
	if (gate->isConnected(hotspot) &&
		gate->getConnection(hotspot) == wire) {
		return nullptr;
	}

	cmdConnectWire *command = new cmdConnectWire(gCircuit, wireId, gateId, hotspot);

	if (command->validateBusLines()) {
		return command;
	}
	else {
		delete command;
		return nullptr;
	}
}

klsCommand * CircuitPage::createGateConnectionCommand(IDType gate1Id, const string &hotspot1, IDType gate2Id, const string &hotspot2) {

	guiGate *gate1 = gateList[gate1Id];
	guiGate *gate2 = gateList[gate2Id];

	// Don't connect a hotspot to itself.
	if (gate1 == gate2 && hotspot1 == hotspot2) {
		return nullptr;
	}

	// Make sure not already connected.
	if (gate1->isConnected(hotspot1) &&
		gate2->isConnected(hotspot2) &&
		gate1->getConnection(hotspot1) == gate2->getConnection(hotspot2)) {
		return nullptr;
	}

	// Neither connected, so create wire.
	if (!gate1->isConnected(hotspot1) &&
		!gate2->isConnected(hotspot2)) {


		vector<IDType> wireIds(gCircuit->getGates()->at(gate1Id)
			->getHotspot(hotspot1)->getBusLines());

		// Get the correct number of new, unique wire ids.
		for (int i = 0; i < (int)wireIds.size(); i++) {
			wireIds[i] = gCircuit->getNextAvailableWireID();
		}

		cmdConnectWire *connectwire =
			new cmdConnectWire(gCircuit, wireIds[0], gate1Id, hotspot1);

		cmdConnectWire *connectwire2 =
			new cmdConnectWire(gCircuit, wireIds[0], gate2Id, hotspot2);

		cmdCreateWire *createWire =
			new cmdCreateWire(this, gCircuit, wireIds, connectwire, connectwire2);

		if (createWire->validateBusLines()) {
			return createWire;
		}
		else {
			delete createWire;
			return nullptr;
		}
	}
	else {
		
		// One of the gates is connected.
		if (gate1->isConnected(hotspot1)) {
			return createGateWireConnectionCommand(gate2Id,
				hotspot2, gate1->getConnection(hotspot1)->getID());
		}
		else if (gate2->isConnected(hotspot2)) {
			return createGateWireConnectionCommand(gate1Id,
				hotspot1, gate2->getConnection(hotspot2)->getID());
		}
		return nullptr;
	}
}

// One step clockwise on screen. The world is y-up, so the model matrix turns
// counter-clockwise for a rising angle -- stepping the stored angle DOWN is what
// reads as clockwise to the user. The angle values keep their meaning, so this
// changes only the order repeated presses walk through them; saved circuits and
// the file format are untouched.
static float rotatedClockwise(const std::string& current) {
	istringstream iss(current);
	float angle = 0.0f;
	iss >> angle;
	return fmod(angle - 90.0f + 360.0f, 360.0f);
}

void CircuitPage::rotateSelection() {

	// If we're placing a gate from quick add menu, rotate that gate
	if (currentDragState == DRAG_NEWGATE && newDragGate != nullptr) {
		ostringstream oss;
		oss << rotatedClockwise(newDragGate->getGUIParam("angle"));
		newDragGate->setGUIParam("angle", oss.str());
		return;
	}

	// If we're in paste mode, rotate all gates being pasted that have no wire connections
	if (isWithinPaste) {
		for (unsigned int i = 0; i < preMove.size(); i++) {
			if (gateList.find(preMove[i].id) == gateList.end()) continue;
			guiGate* gate = gateList[preMove[i].id];

			map< string, GLPoint2f > hotspots = gate->getHotspotList();
			bool hasConnections = false;
			for (auto& hotspot : hotspots) {
				if (gate->isConnected(hotspot.first)) {
					hasConnections = true;
					break;
				}
			}
			if (hasConnections) continue;

			ostringstream oss;
			oss << rotatedClockwise(gate->getGUIParam("angle"));
			gate->setGUIParam("angle", oss.str());
		}
		return;
	}

	// Otherwise rotate all selected gates that aren't connected to wires
	unordered_map< unsigned long, guiGate* >::iterator gateWalk = gateList.begin();
	while (gateWalk != gateList.end()) {
		if (gateWalk->second->isSelected()) {
			// Check if gate has any connections - if so, skip it
			map< string, GLPoint2f > hotspots = gateWalk->second->getHotspotList();
			bool hasConnections = false;
			for (auto& hotspot : hotspots) {
				if (gateWalk->second->isConnected(hotspot.first)) {
					hasConnections = true;
					break;
				}
			}

			// Only rotate if gate has no connections
			if (!hasConnections) {
				ostringstream oss;
				oss << rotatedClockwise(gateWalk->second->getGUIParam("angle"));
				gateWalk->second->setGUIParam("angle", oss.str());
			}
		}
		gateWalk++;
	}
}
