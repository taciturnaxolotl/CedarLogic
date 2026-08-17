#include "sexpr.hpp"

#include "numeric.hpp"

#include <stdexcept>

namespace cl {

const std::string &SNode::head() const {
	static const std::string empty;
	if (isList() && !items.empty() && items.front().kind == Kind::Symbol)
		return items.front().text;
	return empty;
}

const SNode *SNode::child(const std::string &name) const {
	for (const SNode &c : items)
		if (c.isList() && c.head() == name)
			return &c;
	return nullptr;
}

bool SNode::operator==(const SNode &o) const {
	if (kind != o.kind) return false;
	if (kind == Kind::List) return items == o.items;
	return text == o.text;
}

// ---------------------------------------------------------------- writer -----

static void writeAtom(const SNode &n, std::string &out) {
	if (n.kind == SNode::Kind::Symbol) {
		out += n.text;
		return;
	}
	// String: quote it, escaping backslash and double-quote.
	out += '"';
	for (char c : n.text) {
		if (c == '\\' || c == '"') out += '\\';
		out += c;
	}
	out += '"';
}

static void writeNode(const SNode &n, std::string &out, int depth) {
	if (n.kind != SNode::Kind::List) {
		writeAtom(n, out);
		return;
	}
	out += '(';
	bool first = true;
	for (const SNode &c : n.items) {
		if (c.isList()) {
			out += '\n';
			out.append((depth + 1) * 2, ' ');
		} else if (!first) {
			out += ' ';
		}
		writeNode(c, out, depth + 1);
		first = false;
	}
	out += ')';
}

std::string writeSexpr(const SNode &root) {
	std::string out;
	writeNode(root, out, 0);
	out += '\n';
	return out;
}

// ---------------------------------------------------------------- parser -----

namespace {

struct Parser {
	const std::string &s;
	size_t i = 0;
	int depth = 0;

	explicit Parser(const std::string &text) : s(text) {}

	bool atEnd() const { return i >= s.size(); }

	static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
	static bool isDelim(char c) { return isSpace(c) || c == '(' || c == ')' || c == '"'; }

	void skipSpace() {
		while (i < s.size() && isSpace(s[i])) i++;
	}

	SNode parseString() {
		i++; // opening quote
		std::string out;
		while (i < s.size()) {
			char c = s[i++];
			if (c == '\\') {
				if (i >= s.size()) break;
				out += s[i++]; // escaped char taken literally
			} else if (c == '"') {
				return SNode::str(out);
			} else {
				out += c;
			}
		}
		throw std::runtime_error("sexpr: unterminated string");
	}

	SNode parseSymbol() {
		size_t start = i;
		while (i < s.size() && !isDelim(s[i])) i++;
		return SNode::sym(s.substr(start, i - start));
	}

	SNode parseList() {
		// The parser recurses per level, so an unbounded file is a stack overflow.
		// A well-formed v3 document reaches 5 levels and does not grow with the size
		// of the circuit; see cl::kMaxDepth.
		if (++depth > kMaxDepth) throw std::runtime_error("sexpr: nesting too deep");
		struct Pop { int &d; ~Pop() { --d; } } pop{ depth };

		i++; // opening paren
		SNode node = SNode::list();
		while (true) {
			skipSpace();
			if (atEnd()) throw std::runtime_error("sexpr: unbalanced '('");
			if (s[i] == ')') { i++; return node; }
			node.items.push_back(parseNode());
		}
	}

	SNode parseNode() {
		skipSpace();
		if (atEnd()) throw std::runtime_error("sexpr: unexpected end of input");
		char c = s[i];
		if (c == '(') return parseList();
		if (c == ')') throw std::runtime_error("sexpr: unexpected ')'");
		if (c == '"') return parseString();
		return parseSymbol();
	}
};

} // namespace

SNode parseSexpr(const std::string &text) {
	Parser p(text);
	SNode n = p.parseNode();
	return n;
}

} // namespace cl
