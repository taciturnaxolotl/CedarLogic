#include "circuit_file_io.hpp"
#include "sexpr.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace cl {

// ---------------------------------------------------------------- write ------

// Format a number as a bare symbol: integral values without a decimal point,
// others with enough precision to round-trip typical coordinates.
static SNode num(double v) {
	if (v == std::floor(v) && std::abs(v) < 1e15) {
		std::ostringstream o;
		o << static_cast<long long>(v);
		return SNode::sym(o.str());
	}
	std::ostringstream o;
	o.precision(10);
	o << v;
	return SNode::sym(o.str());
}

static SNode kv(const char *head, SNode value) {
	SNode n = SNode::list();
	n.add(SNode::sym(head));
	n.add(std::move(value));
	return n;
}

static SNode gateNode(const GateInstance &g) {
	SNode n = SNode::list();
	n.add(SNode::sym("gate"));
	n.add(SNode::str(g.libName));
	n.add(kv("uuid", SNode::str(g.uuid)));
	SNode at = SNode::list();
	at.add(SNode::sym("at"));
	at.add(num(g.at.x));
	at.add(num(g.at.y));
	n.add(std::move(at));
	n.add(kv("angle", num(g.angle)));
	for (const Param &p : g.params) {
		SNode pn = SNode::list();
		pn.add(SNode::sym(p.gui ? "gparam" : "lparam"));
		pn.add(SNode::str(p.name));
		pn.add(SNode::str(p.value));
		n.add(std::move(pn));
	}
	return n;
}

static SNode wireNode(const WireInstance &w) {
	SNode n = SNode::list();
	n.add(SNode::sym("wire"));
	SNode ids = SNode::list();
	ids.add(SNode::sym("ids"));
	for (const std::string &id : w.ids) ids.add(SNode::str(id));
	n.add(std::move(ids));
	for (const WireSegment &s : w.segments) {
		SNode seg = SNode::list();
		seg.add(SNode::sym("seg"));
		seg.add(SNode::str(s.id));
		seg.add(SNode::sym(s.vertical ? "v" : "h"));
		SNode pts = SNode::list();
		pts.add(SNode::sym("pts"));
		pts.add(num(s.begin.x));
		pts.add(num(s.begin.y));
		pts.add(num(s.end.x));
		pts.add(num(s.end.y));
		seg.add(std::move(pts));
		for (const WireConn &c : s.connects) {
			SNode cn = SNode::list();
			cn.add(SNode::sym("connect"));
			cn.add(SNode::str(c.gateUuid));
			cn.add(SNode::str(c.pin));
			seg.add(std::move(cn));
		}
		for (const Intersection &x : s.intersections) {
			SNode xn = SNode::list();
			xn.add(SNode::sym("cross"));
			xn.add(num(x.at));
			xn.add(SNode::str(x.segment));
			seg.add(std::move(xn));
		}
		n.add(std::move(seg));
	}
	return n;
}

std::string writeCircuitFile(const CircuitFile &cf) {
	SNode root = SNode::list();
	root.add(SNode::sym("cedarlogic"));
	root.add(kv("version", num(cf.formatVersion)));
	root.add(kv("generator", SNode::str(cf.generator)));
	for (const Page &pg : cf.pages) {
		SNode pn = SNode::list();
		pn.add(SNode::sym("page"));
		pn.add(num(pg.index));
		if (pg.hasViewport) {
			SNode vp = SNode::list();
			vp.add(SNode::sym("viewport"));
			vp.add(num(pg.viewTopLeft.x));
			vp.add(num(pg.viewTopLeft.y));
			vp.add(num(pg.viewBottomRight.x));
			vp.add(num(pg.viewBottomRight.y));
			pn.add(std::move(vp));
		}
		for (const GateInstance &g : pg.gates) pn.add(gateNode(g));
		for (const WireInstance &w : pg.wires) pn.add(wireNode(w));
		root.add(std::move(pn));
	}
	return writeSexpr(root);
}

// ---------------------------------------------------------------- read -------

static const SNode &reqChild(const SNode &n, const std::string &name) {
	const SNode *c = n.child(name);
	if (!c) throw std::runtime_error("circuit file: missing (" + name + " ...)");
	return *c;
}

// The atom text of a list's Nth item, e.g. item 1 of (uuid "x") is "x".
static const std::string &item(const SNode &n, size_t idx) {
	if (idx >= n.items.size() || n.items[idx].isList())
		throw std::runtime_error("circuit file: malformed value in (" + n.head() + " ...)");
	return n.items[idx].text;
}

static std::string kvStr(const SNode &n, const std::string &name) {
	return item(reqChild(n, name), 1);
}

static double kvNum(const SNode &n, const std::string &name) {
	return std::stod(item(reqChild(n, name), 1));
}

static GateInstance readGate(const SNode &n) {
	GateInstance g;
	g.libName = item(n, 1);
	g.uuid = kvStr(n, "uuid");
	const SNode &at = reqChild(n, "at");
	g.at = { std::stod(item(at, 1)), std::stod(item(at, 2)) };
	g.angle = kvNum(n, "angle");
	for (const SNode &c : n.items)
		if (c.isList() && (c.head() == "gparam" || c.head() == "lparam"))
			g.params.push_back({ item(c, 1), item(c, 2), c.head() == "gparam" });
	return g;
}

static WireInstance readWire(const SNode &n) {
	WireInstance w;
	if (const SNode *ids = n.child("ids"))
		for (size_t i = 1; i < ids->items.size(); i++) w.ids.push_back(item(*ids, i));
	for (const SNode &s : n.items) {
		if (!s.isList() || s.head() != "seg") continue;
		WireSegment seg;
		seg.id = item(s, 1);
		seg.vertical = (item(s, 2) == "v");
		const SNode &pts = reqChild(s, "pts");
		seg.begin = { std::stod(item(pts, 1)), std::stod(item(pts, 2)) };
		seg.end = { std::stod(item(pts, 3)), std::stod(item(pts, 4)) };
		for (const SNode &c : s.items) {
			if (!c.isList()) continue;
			if (c.head() == "connect") seg.connects.push_back({ item(c, 1), item(c, 2) });
			else if (c.head() == "cross") seg.intersections.push_back({ std::stod(item(c, 1)), item(c, 2) });
		}
		w.segments.push_back(std::move(seg));
	}
	return w;
}

CircuitFile readCircuitFile(const std::string &text) {
	SNode root = parseSexpr(text);
	if (!root.isList() || root.head() != "cedarlogic")
		throw std::runtime_error("circuit file: not a (cedarlogic ...) document");

	CircuitFile cf;
	cf.formatVersion = static_cast<int>(kvNum(root, "version"));
	if (cf.formatVersion != 3)
		throw std::runtime_error("circuit file: unsupported formatVersion " +
		                         std::to_string(cf.formatVersion));
	cf.generator = kvStr(root, "generator");

	for (const SNode &c : root.items) {
		if (!c.isList() || c.head() != "page") continue;
		Page pg;
		pg.index = std::stoi(item(c, 1));
		if (const SNode *vp = c.child("viewport")) {
			pg.hasViewport = true;
			pg.viewTopLeft = { std::stod(item(*vp, 1)), std::stod(item(*vp, 2)) };
			pg.viewBottomRight = { std::stod(item(*vp, 3)), std::stod(item(*vp, 4)) };
		}
		for (const SNode &e : c.items) {
			if (!e.isList()) continue;
			if (e.head() == "gate") pg.gates.push_back(readGate(e));
			else if (e.head() == "wire") pg.wires.push_back(readWire(e));
		}
		cf.pages.push_back(std::move(pg));
	}
	return cf;
}

} // namespace cl
