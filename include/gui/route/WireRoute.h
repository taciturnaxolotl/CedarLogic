// Wire routing seam (Workstream H).
//
// CedarLogic decides a wire's orthogonal shape in guiWire::calcShape -- a
// trunk-and-branch heuristic entangled with guiWire's segment map and its
// float-keyed junction bookkeeping. This header pulls the *decision* out behind
// a small, engine-free interface: pins + hints in, a segment topology out. No
// wxWidgets, no guiGate, no collision types cross it, so a router is a pure,
// deterministic, unit-testable function and a future obstacle-avoiding router
// can replace the trunk one without touching guiWire's plumbing.
//
// Phase 1 ships one implementation, TrunkRouter, a behaviour-preserving port of
// the current calcShape. guiWire adapts its connectPoints/segMap to and from
// these structs.

#ifndef CL_ROUTE_WIREROUTE_H
#define CL_ROUTE_WIREROUTE_H

#include <vector>
#include <utility>

namespace cl {
namespace route {

// A point the wire must reach: a gate hotspot, with whether that hotspot faces
// vertically (top/bottom of the gate) or horizontally (left/right). The pin's
// index in RouteInput::pins is how the caller maps a routed segment's
// connections back to its own connection list.
struct Pin {
	float x = 0.0f;
	float y = 0.0f;
	bool verticalHotspot = false;
};

// One orthogonal segment of the routed shape. begin <= end on the varying axis,
// matching wireSegment's invariant. `pins` lists the RouteInput pin indices that
// physically sit on this segment; `crossings` records each junction as
// (coordinate-along-this-segment, id-of-the-crossing-segment) -- the same
// two-sided bookkeeping guiWire's intersects map uses.
struct Segment {
	long id = 0;
	float bx = 0.0f, by = 0.0f, ex = 0.0f, ey = 0.0f;
	bool vertical = false;
	std::vector<int> pins;
	std::vector<std::pair<float, long>> crossings;
};

struct RouteInput {
	std::vector<Pin> pins;
	// setVerticalBar: when true the router may snap the trunk to the 0.5 grid;
	// when false it reuses trunkPos so a user-positioned trunk survives a recompute.
	bool snapTrunk = true;
	float trunkPos = 0.0f;
	// The wire's running segment-id allocator (guiWire::nextSegID). Passed in and
	// returned so ids stay stable across recomputes exactly as before.
	long nextId = 1;
};

struct RouteResult {
	std::vector<Segment> segments;  // insertion order; each carries its own id
	long nextId = 1;                // allocator after routing
	float trunkPos = 0.0f;          // the trunk coordinate that was used
};

// A wire router: pins + hints -> orthogonal segment topology. Pure and
// deterministic; no engine or app state.
class IWireRouter {
public:
	virtual ~IWireRouter() = default;
	virtual RouteResult route(const RouteInput &in) const = 0;
};

// The historical trunk-and-branch router: a central trunk bar with one branch
// per pin (vertical trunk for horizontal-facing pins, horizontal trunk for
// vertical-facing ones, an L-bend when the two pins face differently). A
// behaviour-preserving extraction of guiWire::calcShape. No obstacle avoidance.
class TrunkRouter : public IWireRouter {
public:
	RouteResult route(const RouteInput &in) const override;
};

}  // namespace route
}  // namespace cl

#endif  // CL_ROUTE_WIREROUTE_H
