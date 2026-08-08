
#pragma once
#include "klsCommand.h"
#include <vector>
#include <map>
#include "../wireSegment.h"
#include "../GUICanvas.h"

// Just a map of wire segments
typedef std::map<long, wireSegment> SegmentMap;

// cmdMoveSelection - move passed gates and wires
class cmdMoveSelection : public klsCommand {
public:
	cmdMoveSelection(GUICircuit* gCircuit, vector<GateState> &preMove,
		vector<WireState> &preMoveWire, float startX, float startY,
		float endX, float endY);

	~cmdMoveSelection();

	bool Do();

	bool Undo();

	vector<klsCommand *> * getConnections();

protected:
	// Re-point a cached segment map's connection gate pointers at the live
	// gates (looked up by gid), so restoring it after a paste undo/redo never
	// dereferences a freed gate.
	void rebindSegMapGates(SegmentMap &segMap);


	vector<unsigned long> gateList;
	vector<unsigned long> wireList;
	map<unsigned long, SegmentMap> oldSegMaps;
	map<unsigned long, SegmentMap> newSegMaps;
	float startX, startY, endX, endY;
	int wireMove;
	vector<klsCommand *> proxconnects;
};