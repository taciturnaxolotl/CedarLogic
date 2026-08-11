/*****************************************************************************
   Project: CEDAR Logic Simulator

   RoutingService: mirrors canvas geometry into a libavoid Router so wires can
   be routed around gates (Workstream H, phase 3.2). Gates become rectangular
   obstacles; wires become orthogonal connectors. This is the phase-3.2b
   skeleton -- 2-terminal connectors only; multi-terminal hyperedges (shared
   trunks) arrive in 3.2d, and canvas wiring + the polyline->segMap translator
   in 3.2c/3.2e. Compiled only when WITH_AVOID is defined.
*****************************************************************************/

#ifndef ROUTINGSERVICE_H_
#define ROUTINGSERVICE_H_

#ifdef WITH_AVOID

#include <vector>
#include <utility>

// Keep libavoid out of this header -- consumers only need the plain types.
namespace Avoid { class Router; class ConnRef; }

namespace cl { namespace avoid {

class RoutingService {
public:
	RoutingService();
	~RoutingService();

	RoutingService(const RoutingService&) = delete;
	RoutingService& operator=(const RoutingService&) = delete;

	// Register a gate's bounding box as an obstacle (world coords, left<right,
	// bottom<top). id is carried through to libavoid for debugging only.
	void addObstacle(unsigned long gateId, float left, float bottom,
	                 float right, float top);

	// Register a 2-point orthogonal connector for a wire, keyed by wireId.
	void addConnector(unsigned long wireId, float sx, float sy,
	                  float ex, float ey);

	// Route every registered connector around the obstacles.
	void run();

	// The routed orthogonal polyline for a wire as (x,y) points; empty if the
	// wire wasn't registered.
	std::vector<std::pair<float, float>> routeOf(unsigned long wireId) const;

private:
	Avoid::Router* router;
	std::vector<std::pair<unsigned long, Avoid::ConnRef*>> conns;
};

}} // namespace cl::avoid

#endif // WITH_AVOID
#endif // ROUTINGSERVICE_H_
