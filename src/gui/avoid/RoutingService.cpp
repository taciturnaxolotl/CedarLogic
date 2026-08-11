/*****************************************************************************
   Project: CEDAR Logic Simulator

   RoutingService: libavoid-backed obstacle-aware routing (Workstream H, 3.2b/d).
   See include/gui/avoid/RoutingService.h.
*****************************************************************************/

#ifdef WITH_AVOID

#include "avoid/RoutingService.h"
#include "libavoid/libavoid.h"

namespace cl { namespace avoid {

RoutingService::RoutingService()
	: router(new Avoid::Router(Avoid::OrthogonalRouting)), nextPinClass(1) {}

RoutingService::~RoutingService() {
	// The Router owns the ShapeRefs/ConnRefs/JunctionRefs/pins registered with
	// it and frees them on destruction, so we don't delete them individually.
	delete router;
}

void RoutingService::addGate(unsigned long gateId, float left, float bottom,
                             float right, float top) {
	Avoid::Rectangle rect(Avoid::Point(left, bottom), Avoid::Point(right, top));
	shapes[gateId] = new Avoid::ShapeRef(router, rect, (unsigned int)gateId);
	gateCentres[gateId] = std::make_pair((left + right) * 0.5f, (bottom + top) * 0.5f);
}

void RoutingService::addPin(unsigned long pinKey, unsigned long gateId,
                            float x, float y) {
	auto s = shapes.find(gateId);
	if (s == shapes.end()) return;
	const std::pair<float, float>& c = gateCentres[gateId];
	unsigned int classId = nextPinClass++;
	// proportional=false -> offsets are lengths from the shape centre; the pin is
	// visible from every direction so a connector can leave it whichever way the
	// route needs (exact pin facing is a 3.2f tuning concern).
	new Avoid::ShapeConnectionPin(s->second, classId,
		(double)(x - c.first), (double)(y - c.second),
		/*proportional=*/false, /*insideOffset=*/0.0, Avoid::ConnDirAll);
	pins[pinKey] = std::make_pair(s->second, classId);
}

void RoutingService::addConnector(unsigned long wireId, unsigned long pinKeyA,
                                  unsigned long pinKeyB) {
	auto a = pins.find(pinKeyA), b = pins.find(pinKeyB);
	if (a == pins.end() || b == pins.end()) return;
	Avoid::ConnRef* c = new Avoid::ConnRef(
		router, Avoid::ConnEnd(a->second.first, a->second.second),
		        Avoid::ConnEnd(b->second.first, b->second.second));
	conns.push_back(std::make_pair(wireId, c));
}

void RoutingService::addHyperedge(unsigned long wireId,
                                  const std::vector<unsigned long>& pinKeys) {
	Avoid::ConnEndList ends;
	for (unsigned long k : pinKeys) {
		auto it = pins.find(k);
		if (it == pins.end()) return; // unknown pin -> skip the whole net
		ends.push_back(Avoid::ConnEnd(it->second.first, it->second.second));
	}
	if (ends.size() < 3) return;
	size_t idx = router->hyperedgeRerouter()->registerHyperedgeForRerouting(ends);
	hyperedges.push_back(std::make_pair(wireId, idx));
}

static std::vector<std::pair<float, float>> polyOf(const Avoid::PolyLine& r) {
	std::vector<std::pair<float, float>> poly;
	for (size_t i = 0; i < r.size(); i++)
		poly.push_back(std::make_pair((float)r.ps[i].x, (float)r.ps[i].y));
	return poly;
}

void RoutingService::run() {
	router->processTransaction();

	// Consume each hyperedge's new-object lists once and cache the geometry.
	for (const auto& he : hyperedges) {
		Avoid::HyperedgeNewAndDeletedObjectLists lists =
			router->hyperedgeRerouter()->newAndDeletedObjectLists(he.second);
		HyperedgeResult res;
		for (Avoid::ConnRef* c : lists.newConnectorList)
			res.routes.push_back(polyOf(c->displayRoute()));
		for (Avoid::JunctionRef* j : lists.newJunctionList) {
			Avoid::Point p = j->position();
			res.junctions.push_back(std::make_pair((float)p.x, (float)p.y));
		}
		hyperedgeResults[he.first] = res;
	}
}

std::vector<std::pair<float, float>>
RoutingService::routeOf(unsigned long wireId) const {
	for (const auto& kv : conns)
		if (kv.first == wireId) return polyOf(kv.second->displayRoute());
	return {};
}

std::vector<std::vector<std::pair<float, float>>>
RoutingService::hyperedgeRoutesOf(unsigned long wireId) const {
	auto it = hyperedgeResults.find(wireId);
	return it == hyperedgeResults.end() ? std::vector<std::vector<std::pair<float, float>>>()
	                                    : it->second.routes;
}

std::vector<std::pair<float, float>>
RoutingService::hyperedgeJunctionsOf(unsigned long wireId) const {
	auto it = hyperedgeResults.find(wireId);
	return it == hyperedgeResults.end() ? std::vector<std::pair<float, float>>()
	                                    : it->second.junctions;
}

}} // namespace cl::avoid

#endif // WITH_AVOID
