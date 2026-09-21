/*****************************************************************************
   Project: CEDAR Logic Simulator

   SegmentMap: a wire's segments, keyed by id.

   Exists to make one mistake impossible. The segments used to live in a plain
   std::map, whose operator[] inserts on a miss, so looking up an id the wire no
   longer had silently grew a blank segment and the caller worked on it anyway.
   That shipped as a crash twice. There is no operator[] here: `at` for an id
   that must exist, `find` for one that might not, `put` to add one on purpose.

   Free of wx, OpenGL and guiGate, so it can be tested without a display.
*****************************************************************************/

#ifndef SEGMENTMAP_H_
#define SEGMENTMAP_H_

#include <cassert>
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

	// The segment with this id, which the caller knows is there.
	wireSegment &at(long id) {
		Store::iterator i = segs_.find(id);
		assert(i != segs_.end() && "segment id not in this wire");
		return i->second;
	}
	const wireSegment &at(long id) const {
		Store::const_iterator i = segs_.find(id);
		assert(i != segs_.end() && "segment id not in this wire");
		return i->second;
	}

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

	// Add or replace a segment, on purpose.
	wireSegment &put(long id, const wireSegment &seg) {
		wireSegment &slot = segs_[id];
		slot = seg;
		return slot;
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
