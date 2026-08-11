/*****************************************************************************
   Project: CEDAR Logic Simulator

   RoutingService: mirrors canvas geometry into a libavoid Router so wires can
   be routed around gates (Workstream H, phase 3.2). Gates become rectangular
   obstacles carrying connection pins at their hotspots; wires become orthogonal
   connectors (2-terminal) or hyperedges (multi-terminal shared trunks).

   Terminals are anchored to shape pins, not free points: libavoid routes
   connectors *outward* from a pin, so endpoints never sit inside their own
   obstacle, and the hyperedge rerouter (which needs shape-anchored terminals)
   builds a valid tree. Compiled only when WITH_AVOID is defined.
*****************************************************************************/

#ifndef ROUTINGSERVICE_H_
#define ROUTINGSERVICE_H_

#ifdef WITH_AVOID

#include <vector>
#include <utility>
#include <map>

// Keep libavoid out of this header -- consumers only need the plain types.
namespace Avoid { class Router; class ConnRef; class ShapeRef; }

namespace cl { namespace avoid {

class RoutingService {
public:
	RoutingService();
	~RoutingService();

	RoutingService(const RoutingService&) = delete;
	RoutingService& operator=(const RoutingService&) = delete;

	// Register a gate as a rectangular obstacle (world coords, left<right,
	// bottom<top), keyed by gateId. Pins are added to it via addPin.
	void addGate(unsigned long gateId, float left, float bottom,
	             float right, float top);

	// Add a connection pin at world coord (x,y) on a previously-added gate.
	// pinKey uniquely identifies the pin within this service; connectors and
	// hyperedges reference pins by that key. No-op if the gate is unknown.
	void addPin(unsigned long pinKey, unsigned long gateId, float x, float y);

	// Register a 2-terminal connector between two pins, keyed by wireId.
	void addConnector(unsigned long wireId, unsigned long pinKeyA,
	                  unsigned long pinKeyB);

	// Register a multi-terminal net (>= 3 pins) as a hyperedge: libavoid places
	// its own junctions and sub-connectors to form a shared-trunk topology.
	void addHyperedge(unsigned long wireId,
	                  const std::vector<unsigned long>& pinKeys);

	// Route every registered connector and hyperedge around the obstacles.
	void run();

	// After run(): the routed orthogonal polyline for a 2-point connector as
	// (x,y) points; empty if the wire wasn't registered as a connector.
	std::vector<std::pair<float, float>> routeOf(unsigned long wireId) const;

	// After run(): the sub-connector polylines of a hyperedge net.
	std::vector<std::vector<std::pair<float, float>>>
	hyperedgeRoutesOf(unsigned long wireId) const;

	// After run(): the junction points libavoid placed for a hyperedge net.
	std::vector<std::pair<float, float>>
	hyperedgeJunctionsOf(unsigned long wireId) const;

private:
	Avoid::Router* router;
	unsigned int nextPinClass;

	std::map<unsigned long, Avoid::ShapeRef*> shapes;                       // gateId -> shape
	std::map<unsigned long, std::pair<float, float>> gateCentres;           // gateId -> centre (for pin offsets)
	std::map<unsigned long, std::pair<Avoid::ShapeRef*, unsigned int>> pins;// pinKey -> (shape, classId)
	std::vector<std::pair<unsigned long, Avoid::ConnRef*>> conns;
	std::vector<std::pair<unsigned long, size_t>> hyperedges;

	// Filled by run(): each hyperedge's result lists are queried exactly once
	// (libavoid's new/deleted lists are meant to be consumed a single time).
	struct HyperedgeResult {
		std::vector<std::vector<std::pair<float, float>>> routes;
		std::vector<std::pair<float, float>> junctions;
	};
	std::map<unsigned long, HyperedgeResult> hyperedgeResults;
};

}} // namespace cl::avoid

#endif // WITH_AVOID
#endif // ROUTINGSERVICE_H_
