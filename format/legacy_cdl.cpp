#include "legacy_cdl.hpp"

#include "numeric.hpp"

#include <sstream>
#include <stdexcept>

namespace cl {

namespace {

// A minimal XML DOM for the legacy format. Tag names may contain spaces (e.g.
// "page 0"). A node keeps its direct text plus child elements; the legacy format
// mixes them (e.g. <input><ID>IN_0</ID>30 </input> has a child and text "30").
struct XmlNode {
	std::string name;
	std::string text;
	std::vector<XmlNode> kids;

	const XmlNode *find(const std::string &n) const {
		for (const XmlNode &k : kids)
			if (k.name == n) return &k;
		return nullptr;
	}
	std::string childText(const std::string &n) const {
		const XmlNode *c = find(n);
		return c ? c->text : std::string();
	}
};

std::string trim(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

struct XmlReader {
	const std::string &s;
	size_t i = 0;
	int depth = 0;
	explicit XmlReader(const std::string &t) : s(t) {}

	static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

	// Legacy CedarLogic escaped '<' inside values as a BEL byte; reverse it.
	static std::string unescape(const std::string &v) {
		std::string out;
		out.reserve(v.size());
		for (char c : v) out += (c == '\x07') ? '<' : c;
		return out;
	}

	std::string readTagBody() {
		i++; // skip '<'
		size_t start = i;
		while (i < s.size() && s[i] != '>') i++;
		if (i >= s.size()) throw std::runtime_error("cdl: unterminated tag");
		std::string body = s.substr(start, i - start);
		i++; // skip '>'
		return body;
	}

	XmlNode parseElement() {
		// Both parsers recurse per level, so an unbounded file is a stack overflow.
		// A well-formed v1/v2 document reaches 7 levels and does not grow with the
		// size of the circuit; see cl::kMaxDepth.
		if (++depth > kMaxDepth) throw std::runtime_error("cdl: nesting too deep");
		struct Pop { int &d; ~Pop() { --d; } } pop{ depth };

		XmlNode node;
		node.name = readTagBody(); // open tag, e.g. "gate" or "page 0"
		std::string text;
		while (i < s.size()) {
			if (s[i] == '<') {
				if (i + 1 < s.size() && s[i + 1] == '/') {
					readTagBody(); // closing tag; not validated against name
					node.text = trim(unescape(text));
					return node;
				}
				node.kids.push_back(parseElement());
			} else {
				text += s[i++];
			}
		}
		throw std::runtime_error("cdl: unclosed <" + node.name + ">");
	}

	std::vector<XmlNode> parseForest() {
		std::vector<XmlNode> nodes;
		while (true) {
			while (i < s.size() && isSpace(s[i])) i++;
			if (i >= s.size()) break;
			if (s[i] != '<') { i++; continue; }
			if (i + 1 < s.size() && s[i + 1] == '/') { readTagBody(); continue; }
			nodes.push_back(parseElement());
		}
		return nodes;
	}
};

std::vector<double> nums(const std::string &csv, const char *what) {
	std::vector<double> out;
	std::stringstream ss(csv);
	std::string tok;
	while (std::getline(ss, tok, ',')) {
		tok = trim(tok);
		if (!tok.empty()) out.push_back(parseDouble(tok, what));
	}
	return out;
}

GateInstance readGate(const XmlNode &g) {
	GateInstance gi;
	gi.uuid = g.childText("ID");
	requireId(gi.uuid, "<gate><ID>", /*allowEmpty=*/false);
	gi.libName = g.childText("type");
	if (gi.libName.empty()) throw std::runtime_error("cdl: <gate> has no <type>");
	std::vector<double> pos = nums(g.childText("position"), "<position>");
	if (pos.size() < 2) throw std::runtime_error("cdl: <position> needs two numbers");
	gi.at = { pos[0], pos[1] };
	for (const XmlNode &c : g.kids) {
		if (c.name != "gparam" && c.name != "lparam") continue;
		size_t sp = c.text.find(' ');
		std::string name = c.text.substr(0, sp);
		std::string value = (sp == std::string::npos) ? "" : c.text.substr(sp + 1);
		if (name == "angle" && c.name == "gparam") {
			gi.angle = value.empty() ? 0.0 : parseDouble(value, "<gparam>angle");
		} else {
			gi.params.push_back({ name, value, c.name == "gparam" });
		}
	}
	return gi;
}

WireInstance readWire(const XmlNode &w) {
	WireInstance wi;
	// A wire's <ID> holds one or more whitespace-separated IDs (bus lines).
	std::istringstream ids(w.childText("ID"));
	std::string id;
	while (ids >> id) {
		requireId(id, "<wire><ID>", /*allowEmpty=*/false);
		wi.ids.push_back(id);
	}

	const XmlNode *shape = w.find("shape");
	if (!shape) return wi;
	for (const XmlNode &seg : shape->kids) {
		if (seg.name != "hsegment" && seg.name != "vsegment") continue;
		WireSegment s;
		s.vertical = (seg.name == "vsegment");
		s.id = seg.childText("ID");
		requireId(s.id, "<hsegment><ID>");
		std::vector<double> p = nums(seg.childText("points"), "<points>");
		if (p.size() < 4) throw std::runtime_error("cdl: <points> needs four numbers");
		s.begin = { p[0], p[1] };
		s.end = { p[2], p[3] };
		for (const XmlNode &c : seg.kids) {
			if (c.name == "connection") {
				requireId(c.childText("GID"), "<connection><GID>");
				s.connects.push_back({ c.childText("GID"), c.childText("name") });
			} else if (c.name == "intersection") {
				// "<isectPoint> <isectSegID>"
				std::istringstream iss(c.text);
				double at = 0;
				std::string other;
				if (!(iss >> at >> other))
					throw std::runtime_error("cdl: <intersection> needs a coordinate and a "
					                         "segment id");
				requireId(other, "<intersection>");
				s.intersections.push_back({ at, other });
			}
		}
		wi.segments.push_back(std::move(s));
	}
	return wi;
}

Page readPage(const XmlNode &p) {
	Page pg;
	std::istringstream iss(p.name); // "page 0"
	std::string word;
	iss >> word >> pg.index;
	for (const XmlNode &c : p.kids) {
		if (c.name == "gate") {
			pg.gates.push_back(readGate(c));
		} else if (c.name == "wire") {
			pg.wires.push_back(readWire(c));
		}
		// <PageViewport> is ignored: pan/zoom is view state, not circuit content.
	}
	return pg;
}

} // namespace

size_t bomLength(const std::string &text) {
	return text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
}

SourceFormat detectFormat(const std::string &text) {
	size_t i = text.find_first_not_of(" \t\r\n", bomLength(text));
	if (i == std::string::npos) return SourceFormat::Unknown;
	if (text[i] == '(')
		return text.find("cedarlogic") != std::string::npos ? SourceFormat::SexprV3
		                                                     : SourceFormat::Unknown;
	if (text.find("<throw_away>") != std::string::npos || text.find("<version>") != std::string::npos)
		return SourceFormat::XmlV2;
	if (text.find("<circuit>") != std::string::npos) return SourceFormat::XmlV1;
	return SourceFormat::Unknown;
}

CircuitFile readLegacyCdl(const std::string &text) {
	XmlReader r(text);
	std::vector<XmlNode> forest = r.parseForest();

	// The real circuit is the last top-level <circuit> (in v2 the first is a decoy).
	const XmlNode *circuit = nullptr;
	for (const XmlNode &n : forest)
		if (n.name == "circuit") circuit = &n;
	if (!circuit) throw std::runtime_error("cdl: no <circuit> element");

	CircuitFile cf;
	cf.formatVersion = 3;
	for (const XmlNode &n : forest)
		if (n.name == "version") cf.generator = "imported from CedarLogic " + n.text;

	for (const XmlNode &child : circuit->kids)
		if (child.name.rfind("page", 0) == 0) // "page N"
			cf.pages.push_back(readPage(child));

	return cf;
}

} // namespace cl
