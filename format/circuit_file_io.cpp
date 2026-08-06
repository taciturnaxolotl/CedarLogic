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
		pn.add(SNode::sym("param"));
		pn.add(SNode::str(p.name));
		pn.add(SNode::str(p.value));
		n.add(std::move(pn));
	}
	return n;
}

static SNode wireNode(const WireInstance &w) {
	SNode n = SNode::list();
	n.add(SNode::sym("wire"));
	n.add(kv("uuid", SNode::str(w.uuid)));
	for (const WireConn &c : w.connects) {
		SNode cn = SNode::list();
		cn.add(SNode::sym("connect"));
		cn.add(kv("uuid", SNode::str(c.gateUuid)));
		cn.add(kv("pin", SNode::str(c.pin)));
		n.add(std::move(cn));
	}
	SNode route = SNode::list();
	route.add(SNode::sym("route"));
	for (const Segment &s : w.route) {
		SNode seg = SNode::list();
		seg.add(SNode::sym("seg"));
		seg.add(num(s.a.x));
		seg.add(num(s.a.y));
		seg.add(num(s.b.x));
		seg.add(num(s.b.y));
		route.add(std::move(seg));
	}
	n.add(std::move(route));
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
		if (c.isList() && c.head() == "param")
			g.params.push_back({ item(c, 1), item(c, 2) });
	return g;
}

static WireInstance readWire(const SNode &n) {
	WireInstance w;
	w.uuid = kvStr(n, "uuid");
	for (const SNode &c : n.items) {
		if (!c.isList()) continue;
		if (c.head() == "connect")
			w.connects.push_back({ kvStr(c, "uuid"), kvStr(c, "pin") });
	}
	if (const SNode *route = n.child("route"))
		for (const SNode &s : route->items)
			if (s.isList() && s.head() == "seg")
				w.route.push_back({ { std::stod(item(s, 1)), std::stod(item(s, 2)) },
				                    { std::stod(item(s, 3)), std::stod(item(s, 4)) } });
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
