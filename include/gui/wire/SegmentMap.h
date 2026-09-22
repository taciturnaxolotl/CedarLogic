/*****************************************************************************
   Project: CEDAR Logic Simulator

   SegmentMap: a wire's segments, keyed by id.

   No operator[], because inserting on a miss is what let a stale id grow a
   blank segment: `at` must exist, `find` may not, `put` adds on purpose.
   Free of wx, OpenGL and guiGate, so it can be tested without a display.
*****************************************************************************/

#ifndef SEGMENTMAP_H_
#define SEGMENTMAP_H_

#include <cstddef>
#include <map>
#include <utility>
#include "wireSegment.h"

namespace cl {
namespace wire {

class SegmentMap {
public:
	typedef std::map< long, wireSegment > Store;

	SegmentMap() {}
	explicit SegmentMap(Store segs) : segs_(std::move(segs)) {}

	// The segment with this id, which the caller knows is there. Checked in
	// every build: a miss here is a bug, and throwing reports it through the
	// crash handler instead of walking off the end of the tree.
	wireSegment &at(long id) { return segs_.at(id); }
	const wireSegment &at(long id) const { return segs_.at(id); }

	// Null when there is no such segment. For an id read back out of an
	// intersects list, which may name a segment that has since gone.
	wireSegment *find(long id) {
		Store::iterator i = segs_.find(id);
		return i == segs_.end() ? NULL : &i->second;
	}
	const wireSegment *find(long id) const {
		Store::const_iterator i = segs_.find(id);
		return i == segs_.end() ? NULL : &i->second;
	}

	bool has(long id) const { return segs_.count(id) != 0; }

	// Add or replace a segment, keyed off the id it carries: passing the key
	// separately is how a segment and its key drift apart.
	wireSegment &put(const wireSegment &seg) {
		std::pair< Store::iterator, bool > r =
			segs_.insert(Store::value_type(seg.id, seg));
		if (!r.second) r.first->second = seg;
		return r.first->second;
	}

	void erase(long id) { segs_.erase(id); }
	void clear() { segs_.clear(); }

	std::size_t size() const { return segs_.size(); }
	bool empty() const { return segs_.empty(); }

	Store::iterator begin() { return segs_.begin(); }
	Store::iterator end() { return segs_.end(); }
	Store::const_iterator begin() const { return segs_.begin(); }
	Store::const_iterator end() const { return segs_.end(); }
	Store::reverse_iterator rbegin() { return segs_.rbegin(); }

	const Store &store() const { return segs_; }

private:
	Store segs_;
};

// Move everything attached to `deadID` onto the nearest segment that has some
// length to it, searching outward through the intersection graph. False when
// there are connections and nowhere to put them.
bool rehomeConnections(SegmentMap &segs, long deadID);

}  // namespace wire
}  // namespace cl

#endif /*SEGMENTMAP_H_*/
