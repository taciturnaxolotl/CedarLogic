#pragma once

#include <string>
#include <vector>

namespace cl {

// A minimal S-expression value: either an atom (a bare Symbol like `gate` or a
// quoted String like "AA_AND2") or a List of child nodes. This is the on-disk
// representation for the v3 .cdl format -- a real grammar with real escaping,
// unlike the hand-rolled pseudo-XML it replaces.
struct SNode {
	enum class Kind { List, Symbol, String };

	Kind kind = Kind::List;
	std::string text;            // payload for Symbol / String
	std::vector<SNode> items;    // children for List

	SNode() = default;

	static SNode list() { SNode n; n.kind = Kind::List; return n; }
	static SNode sym(std::string s) { SNode n; n.kind = Kind::Symbol; n.text = std::move(s); return n; }
	static SNode str(std::string s) { SNode n; n.kind = Kind::String; n.text = std::move(s); return n; }

	bool isList() const { return kind == Kind::List; }

	SNode &add(SNode child) { items.push_back(std::move(child)); return items.back(); }

	// The head symbol of a list, e.g. "gate" for (gate ...); "" if not applicable.
	const std::string &head() const;

	// First child list whose head() == name (e.g. child("uuid") of a gate), or null.
	const SNode *child(const std::string &name) const;

	bool operator==(const SNode &o) const;
	bool operator!=(const SNode &o) const { return !(*this == o); }
};

// Serialize a node to a pretty-printed, diff-friendly string.
std::string writeSexpr(const SNode &root);

// Parse the first top-level S-expression in text. Throws std::runtime_error on
// malformed input (unbalanced parens, unterminated string). When `end` is given,
// it receives the offset one past the last byte consumed, so a caller can check
// what follows the document.
SNode parseSexpr(const std::string &text, size_t *end = nullptr);

} // namespace cl
