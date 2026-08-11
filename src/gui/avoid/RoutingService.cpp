/*****************************************************************************
   Project: CEDAR Logic Simulator

   RoutingService: libavoid-backed obstacle-aware routing (Workstream H, 3.2b).
   See include/gui/avoid/RoutingService.h.
*****************************************************************************/

#ifdef WITH_AVOID

#include "avoid/RoutingService.h"
#include "libavoid/libavoid.h"

namespace cl { namespace avoid {

RoutingService::RoutingService()
	: router(new Avoid::Router(Avoid::OrthogonalRouting)) {}

RoutingService::~RoutingService() {
	// The Router owns the ShapeRefs/ConnRefs registered with it and frees them
	// on destruction, so we don't delete them individually here.
	delete router;
}

void RoutingService::addObstacle(unsigned long gateId, float left, float bottom,
                                 float right, float top) {
	Avoid::Rectangle rect(Avoid::Point(left, bottom), Avoid::Point(right, top));
	new Avoid::ShapeRef(router, rect, (unsigned int)gateId);
}

void RoutingService::addConnector(unsigned long wireId, float sx, float sy,
                                  float ex, float ey) {
	Avoid::ConnRef* c = new Avoid::ConnRef(
		router, Avoid::ConnEnd(Avoid::Point(sx, sy)),
		        Avoid::ConnEnd(Avoid::Point(ex, ey)));
	conns.push_back(std::make_pair(wireId, c));
}

void RoutingService::run() {
	router->processTransaction();
}

std::vector<std::pair<float, float>>
RoutingService::routeOf(unsigned long wireId) const {
	std::vector<std::pair<float, float>> out;
	for (const auto& kv : conns) {
		if (kv.first != wireId) continue;
		const Avoid::PolyLine& r = kv.second->displayRoute();
		for (size_t i = 0; i < r.size(); i++)
			out.push_back(std::make_pair((float)r.ps[i].x, (float)r.ps[i].y));
		break;
	}
	return out;
}

}} // namespace cl::avoid

#endif // WITH_AVOID
