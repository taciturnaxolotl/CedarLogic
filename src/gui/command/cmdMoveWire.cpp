
#include "cmdMoveWire.h"
#include <sstream>
#include "../GUICircuit.h"
#include "../guiWire.h"
#include "cmdSerialize.h"
#include "cmdRegistry.h"

// Self-register so the paste dispatcher can rebuild a "movewire ..." line
// without naming this type. See cmdRegistry.h.
static const bool s_registered_cmdMoveWire =
	cmd::registerFactory("movewire", [](const std::string &def) -> klsCommand * {
		return new cmdMoveWire(def);
	});

cmdMoveWire::cmdMoveWire(GUICircuit* gCircuit, unsigned long wid,
		const SegmentMap &oldList, const SegmentMap &newList) :
			klsCommand(true, "Move Wire") {

	this->gCircuit = gCircuit;
	this->wid = wid;
	oldSegList = oldList;
	newSegList = newList;
	delta = GLPoint2f(0, 0);
}

cmdMoveWire::cmdMoveWire(GUICircuit* gCircuit, unsigned long wid,
		const SegmentMap &oldList, GLPoint2f delta) :
			klsCommand(true, "Move Wire") {

	this->gCircuit = gCircuit;
	this->wid = wid;
	oldSegList = oldList;
	this->delta = delta;
}

cmdMoveWire::cmdMoveWire(string def) : klsCommand(true, "Move Wire") {

	cmdser::MoveWire m;
	cmdser::parse(def, m);
	wid = m.wid;
	for (const cmdser::MoveWireSeg &s : m.segments) {
		wireSegment seg(GLPoint2f(s.bx, s.by), GLPoint2f(s.ex, s.ey), s.vertical, s.id);
		for (const auto &conn : s.connections) {
			wireConnection wc;
			wc.gid = conn.first;
			wc.connection = conn.second;
			seg.connections.push_back(wc);
		}
		for (const auto &isect : s.intersects)
			seg.intersects[isect.first].push_back(isect.second);
		newSegList[s.id] = seg;
	}
	oldSegList = newSegList;
	delta = GLPoint2f(0, 0);
}

bool cmdMoveWire::Do() {

	if ((gCircuit->getWires())->find(wid) == (gCircuit->getWires())->end()) return false; // error, wire not found

	if (delta.x != 0 || delta.y != 0) {
		map < long, wireSegment >::iterator segWalk = oldSegList.begin();
		while (segWalk != oldSegList.end()) {
			(segWalk->second).begin.x += delta.x; (segWalk->second).begin.y += delta.y;
			(segWalk->second).end.x += delta.x; (segWalk->second).end.y += delta.y;
			segWalk++;
		}
		(*(gCircuit->getWires()))[wid]->setSegmentMap(oldSegList);
	}
	else {
		(*(gCircuit->getWires()))[wid]->setSegmentMap(newSegList);
	}
	(*(gCircuit->getWires()))[wid]->endSegDrag();
	return true;
}

bool cmdMoveWire::Undo() {

	if ((gCircuit->getWires())->find(wid) == (gCircuit->getWires())->end()) return false; // error, wire not found

	if (delta.x != 0 || delta.y != 0) {
		map < long, wireSegment >::iterator segWalk = oldSegList.begin();
		while (segWalk != oldSegList.end()) {
			(segWalk->second).begin.x -= delta.x; (segWalk->second).begin.y -= delta.y;
			(segWalk->second).end.x -= delta.x; (segWalk->second).end.y -= delta.y;
			segWalk++;
		}
	}
	(*(gCircuit->getWires()))[wid]->setSegmentMap(oldSegList);
	return true;
}

string cmdMoveWire::toString() const {

	if ((gCircuit->getWires())->find(wid) == (gCircuit->getWires())->end()) return ""; // error, wire not found

	// Mirror the segment map into the GUI-free struct, preserving map (sorted)
	// order, then let cmdser emit the exact byte format.
	cmdser::MoveWire m;
	m.wid = wid;
	for (const auto &kv : newSegList) {
		const wireSegment &seg = kv.second;
		cmdser::MoveWireSeg s;
		s.id = seg.id;
		s.vertical = seg.isVertical();
		s.bx = seg.begin.x; s.by = seg.begin.y; s.ex = seg.end.x; s.ey = seg.end.y;
		for (unsigned int i = 0; i < seg.connections.size(); i++)
			s.connections.push_back({ seg.connections[i].gid, seg.connections[i].connection });
		for (const auto &isect : seg.intersects)
			for (long sid : isect.second)
				s.intersects.push_back({ isect.first, sid });
		m.segments.push_back(s);
	}
	return cmdser::emit(m);
}

void cmdMoveWire::setPointers(GUICircuit* gCircuit, GUICanvas* gCanvas,
		TranslationMap &gateids, TranslationMap &wireids) {

	this->gCircuit = gCircuit;
	wid = wireids[wid];
	map < long, wireSegment >::iterator segWalk = newSegList.begin();
	while (segWalk != newSegList.end()) {
		for (unsigned int i = 0; i < (segWalk->second).connections.size(); i++) {
			(segWalk->second).connections[i].gid = gateids[(segWalk->second).connections[i].gid];
		}
		segWalk++;
	}
	oldSegList = newSegList;
}
