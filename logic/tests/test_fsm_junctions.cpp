// A two-flip-flop state machine built the way a student builds one: NOR logic
// fed through named FROM/TO links, LEDs (NODE junctions) on the state bits, a
// one-step PULSE for the clock and DRIVERs for the inputs. Every NOR is checked
// against its own input wires after each settle, through thousands of cycles
// with the clock and inputs poked at random moments. This is the shape of
// circuit a "random gate shows the wrong value" report came from; the core
// held up and the fault was in the GUI (a gate rebuilt by undo lost its
// inverted pins), so this stands guard over the part that was right.
#include <doctest/doctest.h>

#include "logic_circuit.h"
#include "logic_values.h"
#include <string>
#include <vector>
#include <map>
#include <iostream>

namespace {

struct Fsm {
	Circuit c;
	// gate id -> (type) for invariant checks
	struct NorInfo { IDType id; std::vector<IDType> inWires; IDType outWire; };
	std::vector<NorInfo> nors;

	IDType gate(IDType id, const std::string &type) {
		IDType g = c.newGate(type, id);
		REQUIRE(g == id);
		return g;
	}
	void in(IDType g, const std::string &pin, IDType w) { c.connectGateInput(g, pin, w); }
	void out(IDType g, const std::string &pin, IDType w) { c.connectGateOutput(g, pin, w); }
	void inv(IDType g, const std::string &pin) { c.setGateOutputParameter(g, pin, "INVERTED", "TRUE"); }
	void invIn(IDType g, const std::string &pin) { c.setGateInputParameter(g, pin, "INVERTED", "TRUE"); }

	void nor(IDType id, int bits, std::vector<IDType> ins, IDType outw) {
		gate(id, "OR");
		c.setGateParameter(id, "INPUT_BITS", std::to_string(bits));
		inv(id, "OUT");
		for (int i = 0; i < bits; i++) in(id, "IN_" + std::to_string(i), ins[i]);
		out(id, "OUT", outw);
		nors.push_back({id, ins, outw});
	}
	void toggle(IDType id, int v, IDType w) {
		gate(id, "DRIVER");
		c.setGateParameter(id, "OUTPUT_BITS", "1");
		c.setGateParameter(id, "OUTPUT_NUM", std::to_string(v));
		out(id, "OUT_0", w);
	}
	void from(IDType id, const std::string &name, IDType w) {
		gate(id, "FROM");
		c.setGateParameter(id, "JUNCTION_ID", "F");
		c.setGateParameter(id, "JUNCTION_ID", name);
		if (w != ID_NONE) in(id, "IN_0", w);
	}
	void to(IDType id, const std::string &name, IDType w) {
		gate(id, "TO");
		c.setGateParameter(id, "JUNCTION_ID", "TO");
		c.setGateParameter(id, "JUNCTION_ID", name);
		if (w != ID_NONE) in(id, "IN_0", w);
	}
	void led(IDType id, IDType w) {
		gate(id, "NODE");
		in(id, "N_in0", w);
	}
	void dff(IDType id, int cur, IDType d, IDType clk, IDType set, IDType clr, IDType q, IDType qn) {
		gate(id, "REGISTER");
		c.setGateParameter(id, "INPUT_BITS", "1");
		c.setGateParameter(id, "NO_HOLD", "true");
		c.setGateParameter(id, "SYNC_LOAD", "true");
		c.setGateParameter(id, "SYNC_SET", "false");
		c.setGateParameter(id, "SYNC_CLEAR", "false");
		c.setGateParameter(id, "CURRENT_VALUE", std::to_string(cur));
		invIn(id, "set");
		invIn(id, "clear");
		in(id, "IN_0", d);
		in(id, "clock", clk);
		in(id, "set", set);
		in(id, "clear", clr);
		out(id, "OUT_0", q);
		out(id, "OUTINV_0", qn);
	}
	void inverter(IDType id, IDType i, IDType o) {
		gate(id, "BUFFER");
		c.setGateParameter(id, "INPUT_BITS", "1");
		c.setGateParameter(id, "DEFAULT_DELAY", "1");
		inv(id, "OUT_0");
		in(id, "IN_0", i);
		out(id, "OUT_0", o);
	}

	void build() {
		led(53, 81);
		gate(58, "PULSE");
		out(58, "OUT_0", 80);
		toggle(59, 0, 91);
		dff(92, 1, 84, 80, 104, 105, 88, 82);
		dff(93, 0, 94, 80, 106, 107, 83, 89);
		nor(95, 2, {82, 83}, 81);
		nor(97, 3, {110, 109, 111}, 85);
		nor(98, 3, {85, 86, 87}, 84);
		nor(99, 2, {114, 115}, 86);
		nor(100, 2, {113, 112}, 87);
		from(102, "X1", ID_NONE);
		from(103, "X2", ID_NONE);
		from(104, "X2'", ID_NONE);
		from(105, "X1'", ID_NONE);
		from(106, "Q2", ID_NONE);
		from(107, "Q1", ID_NONE);
		from(108, "Q1'", ID_NONE);
		from(109, "Q2'", ID_NONE);
		to(111, "Q1", 88);
		to(112, "Q2", 83);
		to(113, "Q2'", 89);
		to(114, "Q1'", 82);
		toggle(116, 0, 90);
		inverter(118, 90, 92);
		inverter(119, 91, 93);
		to(120, "X1", 90);
		to(121, "X1'", 92);
		to(122, "X2'", 93);
		to(123, "X2", 91);
		nor(124, 2, {95, 96}, 94);
		nor(125, 2, {98, 99}, 95);
		nor(126, 2, {108, 97}, 96);
		nor(128, 2, {102, 103}, 97);
		nor(129, 2, {101, 100}, 98);
		from(130, "Q1'", 99);
		from(131, "X2", 100);
		from(132, "X1", 101);
		from(133, "X1'", 102);
		from(134, "X2'", 103);
		toggle(135, 1, 104);
		toggle(136, 1, 105);
		toggle(137, 1, 106);
		toggle(138, 1, 107);
		from(139, "Q2", 108);
		from(140, "X1'", 110);
		from(141, "X2'", 109);
		from(142, "Q1", 111);
		from(143, "Q2", 112);
		from(144, "X1'", 113);
		from(145, "Q1", 114);
		from(146, "Q2", 115);
		led(148, 88);
		led(149, 83);
	}

	void step(int n) {
		for (int i = 0; i < n; i++) { ID_SET<IDType> ch; c.step(&ch); }
	}

	bool checkNors(const std::string &where) {
		bool ok = true;
		for (auto &n : nors) {
			StateType expect = ONE;
			bool unknown = false;
			for (IDType w : n.inWires) {
				StateType s = c.getWireState(w);
				if (s == ONE) expect = ZERO;
				else if (s != ZERO) unknown = true;
			}
			if (unknown && expect == ONE) expect = UNKNOWN;
			StateType got = c.getWireState(n.outWire);
			if (got != expect) {
				ok = false;
				std::cerr << where << ": NOR " << n.id << " inputs";
				for (IDType w : n.inWires) std::cerr << " w" << w << "=" << (int)c.getWireState(w);
				std::cerr << " out w" << n.outWire << "=" << (int)got << " expected " << (int)expect << "\n";
			}
		}
		return ok;
	}
};

} // namespace

TEST_CASE("NOR state machine over named junctions stays consistent across many cycles") {
	Fsm h;
	h.build();
	h.step(10);
	REQUIRE(h.checkNors("initial"));

	unsigned seed = 12345;
	auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };

	for (int cycle = 0; cycle < 1500; cycle++) {
		int acts = 1 + rnd() % 4;
		for (int a = 0; a < acts; a++) {
			int r = rnd() % 5;
			if (r == 0) h.c.setGateParameter(116, "OUTPUT_NUM", std::to_string(rnd() % 2));
			if (r == 1) h.c.setGateParameter(59, "OUTPUT_NUM", std::to_string(rnd() % 2));
			if (r == 2 || r == 3) h.c.setGateParameter(58, "PULSE", "1");
			if (r == 4) h.c.setGateParameter(58, "PULSE", std::to_string(1 + rnd() % 3));
			h.step(rnd() % 4);
		}
		h.step(10);
		if (!h.checkNors("cycle " + std::to_string(cycle))) {
			h.step(20);
			CHECK(h.checkNors("cycle " + std::to_string(cycle) + " +20"));
			break;
		}
	}
}
