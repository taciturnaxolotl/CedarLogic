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

static SNode gateDefNode(const GateDef &g) {
	SNode n = SNode::list();
	n.add(SNode::sym("gatedef"));
	n.add(SNode::str(g.name));
	n.add(kv("caption", SNode::str(g.caption)));
	n.add(kv("gui", SNode::str(g.guiType)));
	n.add(kv("logic", SNode::str(g.logicType)));
	for (const HotspotDef &h : g.hotspots) {
		SNode hn = SNode::list();
		hn.add(SNode::sym("hotspot"));
		hn.add(SNode::str(h.name));
		hn.add(SNode::sym(h.isInput ? "in" : "out"));
		hn.add(num(h.x));
		hn.add(num(h.y));
		if (h.inverted) hn.add(SNode::sym("inv"));
		if (h.busLines != 1) hn.add(kv("bus", num(h.busLines)));
		if (!h.eInput.empty()) hn.add(kv("e", SNode::str(h.eInput)));
		n.add(std::move(hn));
	}
	for (const LineDef &l : g.shape) {
		SNode ln = SNode::list();
		ln.add(SNode::sym(l.isLabel ? "labelline" : "line"));
		ln.add(num(l.x1));
		ln.add(num(l.y1));
		ln.add(num(l.x2));
		ln.add(num(l.y2));
		n.add(std::move(ln));
	}
	for (const DlgParamDef &d : g.dlgParams) {
		SNode dn = SNode::list();
		dn.add(SNode::sym("dlgparam"));
		dn.add(SNode::str(d.label));
		dn.add(SNode::str(d.name));
		dn.add(SNode::str(d.type));
		dn.add(SNode::sym(d.isGui ? "gui" : "logic"));
		// Emit range bounds only when the param actually constrains a number, so
		// the common unbounded case stays free of noisy sentinel tokens (and old
		// readers that stop at the gui/logic symbol still load it).
		if (d.rMin != std::numeric_limits<float>::lowest() ||
		    d.rMax != std::numeric_limits<float>::max()) {
			dn.add(num(d.rMin));
			dn.add(num(d.rMax));
		}
		n.add(std::move(dn));
	}
	for (const Param &p : g.params) {
		SNode pn = SNode::list();
		pn.add(SNode::sym(p.gui ? "gparam" : "lparam"));
		pn.add(SNode::str(p.name));
		pn.add(SNode::str(p.value));
		n.add(std::move(pn));
	}
	return n;
}

std::string writeCircuitFile(const CircuitFile &cf) {
	SNode root = SNode::list();
	root.add(SNode::sym("cedarlogic"));
	root.add(kv("version", num(cf.formatVersion)));
	root.add(kv("generator", SNode::str(cf.generator)));
	for (const GateDef &g : cf.usedGates) root.add(gateDefNode(g));
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

static std::string optStr(const SNode &n, const std::string &name) {
	const SNode *c = n.child(name);
	return c ? item(*c, 1) : std::string();
}

static GateDef readGateDef(const SNode &n) {
	GateDef g;
	g.name = item(n, 1);
	g.caption = optStr(n, "caption");
	g.guiType = optStr(n, "gui");
	g.logicType = optStr(n, "logic");
	for (const SNode &c : n.items) {
		if (!c.isList()) continue;
		const std::string &head = c.head();
		if (head == "hotspot") {
			HotspotDef h;
			h.name = item(c, 1);
			h.isInput = (item(c, 2) == "in");
			h.x = std::stod(item(c, 3));
			h.y = std::stod(item(c, 4));
			for (size_t i = 5; i < c.items.size(); i++) {
				const SNode &f = c.items[i];
				if (!f.isList()) { if (f.text == "inv") h.inverted = true; }
				else if (f.head() == "bus") h.busLines = static_cast<int>(std::stod(item(f, 1)));
				else if (f.head() == "e") h.eInput = item(f, 1);
			}
			g.hotspots.push_back(std::move(h));
		} else if (head == "line" || head == "labelline") {
			LineDef l;
			l.x1 = std::stod(item(c, 1));
			l.y1 = std::stod(item(c, 2));
			l.x2 = std::stod(item(c, 3));
			l.y2 = std::stod(item(c, 4));
			l.isLabel = (head == "labelline");
			g.shape.push_back(l);
		} else if (head == "dlgparam") {
			DlgParamDef d;
			d.label = item(c, 1);
			d.name = item(c, 2);
			d.type = item(c, 3);
			d.isGui = (item(c, 4) == "gui");
			// Optional range bounds; absent means unbounded (keep the defaults).
			if (c.items.size() > 5) d.rMin = std::stof(item(c, 5));
			if (c.items.size() > 6) d.rMax = std::stof(item(c, 6));
			g.dlgParams.push_back(std::move(d));
		} else if (head == "gparam" || head == "lparam") {
			g.params.push_back({ item(c, 1), item(c, 2), head == "gparam" });
		}
	}
	return g;
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

	for (const SNode &c : root.items)
		if (c.isList() && c.head() == "gatedef") cf.usedGates.push_back(readGateDef(c));

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
