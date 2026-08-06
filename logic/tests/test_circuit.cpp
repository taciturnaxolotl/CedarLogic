// Behavioral tests for the shipping logic engine, driving the real Circuit API
// (newGate / connect / step / getWireState) end-to-end. These port the proven
// recipes from wasm/test_all_gates.mjs into C++ and extend them toward gate
// coverage, tri-state resolution, and edit-during-simulation cases.
//
// NOTE: this TU intentionally does NOT define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// -- test.cpp provides main() for the test_logic executable.
#include <doctest/doctest.h>

#include "logic_circuit.h"
#include "logic_values.h"
#include <string>

namespace {

// Step the circuit forward n times, discarding the changed-wire set.
void stepN(Circuit &c, int n) {
	for (int i = 0; i < n; i++) {
		ID_SET<IDType> changed;
		c.step(&changed);
	}
}

// Create a 1-bit DRIVER gate outputting `value` (0/1) and return its gate ID.
// Mirrors makeDriver() in the wasm harness.
IDType makeDriver(Circuit &c, int value) {
	IDType drv = c.newGate("DRIVER");
	c.setGateParameter(drv, "OUTPUT_BITS", "1");
	c.setGateParameter(drv, "OUTPUT_NUM", std::to_string(value));
	return drv;
}

// Build `type`(a, b) as a 2-input, 1-bit gate driven by two DRIVERs, settle the
// circuit, and return the output wire's state.
StateType eval2(const std::string &type, int a, int b) {
	Circuit c;
	IDType gate = c.newGate(type);
	c.setGateParameter(gate, "INPUT_BITS", "2");

	IDType d0 = makeDriver(c, a), d1 = makeDriver(c, b);
	IDType w0 = c.newWire(), w1 = c.newWire(), wOut = c.newWire();

	c.connectGateOutput(d0, "OUT_0", w0);
	c.connectGateOutput(d1, "OUT_0", w1);
	c.connectGateInput(gate, "IN_0", w0);
	c.connectGateInput(gate, "IN_1", w1);
	c.connectGateOutput(gate, "OUT", wOut);

	stepN(c, 5);
	return c.getWireState(wOut);
}

} // namespace

TEST_CASE("AND gate truth table") {
	CHECK(eval2("AND", 1, 1) == ONE);
	CHECK(eval2("AND", 1, 0) == ZERO);
	CHECK(eval2("AND", 0, 1) == ZERO);
	CHECK(eval2("AND", 0, 0) == ZERO);
}

TEST_CASE("OR gate truth table") {
	CHECK(eval2("OR", 1, 1) == ONE);
	CHECK(eval2("OR", 1, 0) == ONE);
	CHECK(eval2("OR", 0, 1) == ONE);
	CHECK(eval2("OR", 0, 0) == ZERO);
}

TEST_CASE("XOR gate truth table") {
	CHECK(eval2("XOR", 1, 1) == ZERO);
	CHECK(eval2("XOR", 1, 0) == ONE);
	CHECK(eval2("XOR", 0, 1) == ONE);
	CHECK(eval2("XOR", 0, 0) == ZERO);
}

TEST_CASE("EQUIVALENCE (XNOR) gate truth table") {
	CHECK(eval2("EQUIVALENCE", 1, 1) == ONE);
	CHECK(eval2("EQUIVALENCE", 1, 0) == ZERO);
	CHECK(eval2("EQUIVALENCE", 0, 1) == ZERO);
	CHECK(eval2("EQUIVALENCE", 0, 0) == ONE);
}

TEST_CASE("BUFFER passes its input through") {
	Circuit c;
	IDType buf = c.newGate("BUFFER");
	c.setGateParameter(buf, "INPUT_BITS", "1");
	IDType drv = makeDriver(c, 1);
	IDType wIn = c.newWire(), wOut = c.newWire();
	c.connectGateOutput(drv, "OUT_0", wIn);
	c.connectGateInput(buf, "IN_0", wIn);
	c.connectGateOutput(buf, "OUT_0", wOut);
	stepN(c, 5);
	CHECK(c.getWireState(wOut) == ONE);
}

TEST_CASE("N-input AND requires all inputs high") {
	Circuit c;
	IDType gate = c.newGate("AND");
	c.setGateParameter(gate, "INPUT_BITS", "3");
	IDType wOut = c.newWire();
	c.connectGateOutput(gate, "OUT", wOut);

	// Drive IN_0..IN_2; hold IN_2 low first, then high.
	IDType d0 = makeDriver(c, 1), d1 = makeDriver(c, 1), d2 = makeDriver(c, 0);
	IDType w0 = c.newWire(), w1 = c.newWire(), w2 = c.newWire();
	c.connectGateOutput(d0, "OUT_0", w0);
	c.connectGateOutput(d1, "OUT_0", w1);
	c.connectGateOutput(d2, "OUT_0", w2);
	c.connectGateInput(gate, "IN_0", w0);
	c.connectGateInput(gate, "IN_1", w1);
	c.connectGateInput(gate, "IN_2", w2);
	stepN(c, 5);
	CHECK(c.getWireState(wOut) == ZERO); // one input low -> low

	c.setGateParameter(d2, "OUTPUT_NUM", "1"); // now all high
	stepN(c, 5);
	CHECK(c.getWireState(wOut) == ONE);
}

TEST_CASE("A wire with no driver sits at HI_Z") {
	Circuit c;
	IDType w = c.newWire();
	stepN(c, 2);
	CHECK(c.getWireState(w) == HI_Z);
}

TEST_CASE("Two drivers fighting on one wire resolve to CONFLICT") {
	Circuit c;
	IDType hi = makeDriver(c, 1), lo = makeDriver(c, 0);
	IDType w = c.newWire();
	c.connectGateOutput(hi, "OUT_0", w);
	c.connectGateOutput(lo, "OUT_0", w);
	stepN(c, 5);
	CHECK(c.getWireState(w) == CONFLICT);
}

TEST_CASE("CLOCK toggles over time") {
	Circuit c;
	IDType clk = c.newGate("CLOCK");
	c.setGateParameter(clk, "HALF_CYCLE", "2");
	IDType wOut = c.newWire();
	c.connectGateOutput(clk, "CLK", wOut);

	StateType prev = c.getWireState(wOut);
	int toggles = 0;
	for (int i = 0; i < 10; i++) {
		ID_SET<IDType> changed;
		c.step(&changed);
		StateType now = c.getWireState(wOut);
		if (now != prev) toggles++;
		prev = now;
	}
	CHECK(toggles >= 2);
}

TEST_CASE("JK flip-flop sets Q on J=1,K=0") {
	Circuit c;
	IDType jkff = c.newGate("JKFF");
	IDType clk = c.newGate("CLOCK");
	c.setGateParameter(clk, "HALF_CYCLE", "1");
	IDType wClk = c.newWire();
	c.connectGateOutput(clk, "CLK", wClk);
	c.connectGateInput(jkff, "clock", wClk);

	IDType drvJ = makeDriver(c, 1), drvK = makeDriver(c, 0);
	IDType wJ = c.newWire(), wK = c.newWire(), wQ = c.newWire();
	c.connectGateOutput(drvJ, "OUT_0", wJ);
	c.connectGateOutput(drvK, "OUT_0", wK);
	c.connectGateInput(jkff, "J", wJ);
	c.connectGateInput(jkff, "K", wK);
	c.connectGateOutput(jkff, "Q", wQ);
	stepN(c, 10);
	CHECK(c.getWireState(wQ) == ONE);
}

TEST_CASE("2:1 MUX selects the addressed input") {
	Circuit c;
	IDType mux = c.newGate("MUX");
	c.setGateParameter(mux, "INPUT_BITS", "2");
	IDType drvA = makeDriver(c, 0), drvB = makeDriver(c, 1), drvSel = makeDriver(c, 1);
	IDType wA = c.newWire(), wB = c.newWire(), wSel = c.newWire(), wOut = c.newWire();
	c.connectGateOutput(drvA, "OUT_0", wA);
	c.connectGateOutput(drvB, "OUT_0", wB);
	c.connectGateOutput(drvSel, "OUT_0", wSel);
	c.connectGateInput(mux, "IN_0", wA);
	c.connectGateInput(mux, "IN_1", wB);
	c.connectGateInput(mux, "SEL_0", wSel);
	c.connectGateOutput(mux, "OUT", wOut);
	stepN(c, 5);
	CHECK(c.getWireState(wOut) == ONE); // sel=1 -> IN_1 = 1
}

TEST_CASE("2-bit ADDER computes sum and carry") {
	Circuit c;
	IDType add = c.newGate("ADDER");
	c.setGateParameter(add, "INPUT_BITS", "2");

	// A = 01 (1), B = 11 (3) -> sum = 4 -> OUT = 00, carry_out = 1.
	IDType a0 = makeDriver(c, 1), a1 = makeDriver(c, 0); // A = 01 = 1
	IDType b0 = makeDriver(c, 1), b1 = makeDriver(c, 1); // B = 11 = 3
	IDType cin = makeDriver(c, 0);
	IDType wA0 = c.newWire(), wA1 = c.newWire(), wB0 = c.newWire(), wB1 = c.newWire();
	IDType wCin = c.newWire(), wOut0 = c.newWire(), wOut1 = c.newWire(), wCout = c.newWire();

	c.connectGateOutput(a0, "OUT_0", wA0);
	c.connectGateOutput(a1, "OUT_0", wA1);
	c.connectGateOutput(b0, "OUT_0", wB0);
	c.connectGateOutput(b1, "OUT_0", wB1);
	c.connectGateOutput(cin, "OUT_0", wCin);
	c.connectGateInput(add, "IN_0", wA0);
	c.connectGateInput(add, "IN_1", wA1);
	c.connectGateInput(add, "IN_B_0", wB0);
	c.connectGateInput(add, "IN_B_1", wB1);
	c.connectGateInput(add, "carry_in", wCin);
	c.connectGateOutput(add, "OUT_0", wOut0);
	c.connectGateOutput(add, "OUT_1", wOut1);
	c.connectGateOutput(add, "carry_out", wCout);

	stepN(c, 5);
	CHECK(c.getWireState(wOut0) == ZERO); // 1 + 3 = 4 -> low 2 bits are 00
	CHECK(c.getWireState(wOut1) == ZERO);
	CHECK(c.getWireState(wCout) == ONE);  // carry out of a 2-bit add
}

TEST_CASE("2-bit COMPARE magnitude comparator") {
	// Build a comparator over 2-bit A (IN) and B (IN_B), read one output pin.
	// The cascade inputs (in_A_equal_B/greater/less) are left floating, which
	// the gate treats as "equal favored".
	auto cmpOut = [](int a, int b, const char *outPin) {
		Circuit c;
		IDType cmp = c.newGate("COMPARE");
		c.setGateParameter(cmp, "INPUT_BITS", "2");
		IDType dA0 = makeDriver(c, a & 1), dA1 = makeDriver(c, (a >> 1) & 1);
		IDType dB0 = makeDriver(c, b & 1), dB1 = makeDriver(c, (b >> 1) & 1);
		IDType wA0 = c.newWire(), wA1 = c.newWire(), wB0 = c.newWire(), wB1 = c.newWire(), wOut = c.newWire();
		c.connectGateOutput(dA0, "OUT_0", wA0);
		c.connectGateOutput(dA1, "OUT_0", wA1);
		c.connectGateOutput(dB0, "OUT_0", wB0);
		c.connectGateOutput(dB1, "OUT_0", wB1);
		c.connectGateInput(cmp, "IN_0", wA0);
		c.connectGateInput(cmp, "IN_1", wA1);
		c.connectGateInput(cmp, "IN_B_0", wB0);
		c.connectGateInput(cmp, "IN_B_1", wB1);
		c.connectGateOutput(cmp, outPin, wOut);
		stepN(c, 5);
		return c.getWireState(wOut);
	};
	CHECK(cmpOut(2, 1, "A_greater_B") == ONE); // 2 > 1
	CHECK(cmpOut(2, 1, "A_less_B") == ZERO);
	CHECK(cmpOut(2, 1, "A_equal_B") == ZERO);
	CHECK(cmpOut(1, 1, "A_equal_B") == ONE); // 1 == 1
	CHECK(cmpOut(0, 2, "A_less_B") == ONE);  // 0 < 2
}

TEST_CASE("2-to-4 DECODER activates the addressed output line") {
	// Enable pins (ENABLE/ENABLE_B/ENABLE_C) are left floating = enabled.
	Circuit c;
	IDType dec = c.newGate("DECODER");
	c.setGateParameter(dec, "INPUT_BITS", "2");
	IDType d0 = makeDriver(c, 0), d1 = makeDriver(c, 1); // IN = 10b = 2
	IDType wIn0 = c.newWire(), wIn1 = c.newWire();
	c.connectGateOutput(d0, "OUT_0", wIn0);
	c.connectGateOutput(d1, "OUT_0", wIn1);
	c.connectGateInput(dec, "IN_0", wIn0);
	c.connectGateInput(dec, "IN_1", wIn1);
	IDType wO0 = c.newWire(), wO1 = c.newWire(), wO2 = c.newWire(), wO3 = c.newWire();
	c.connectGateOutput(dec, "OUT_0", wO0);
	c.connectGateOutput(dec, "OUT_1", wO1);
	c.connectGateOutput(dec, "OUT_2", wO2);
	c.connectGateOutput(dec, "OUT_3", wO3);
	stepN(c, 5);
	CHECK(c.getWireState(wO2) == ONE); // address 2 selected
	CHECK(c.getWireState(wO0) == ZERO);
	CHECK(c.getWireState(wO1) == ZERO);
	CHECK(c.getWireState(wO3) == ZERO);
}

TEST_CASE("3-to-8 DECODER has exactly 8 outputs and selects the top one") {
	// Regression for the width fix: INPUT_BITS 3 must give 2^3 = 8 outputs
	// (OUT_0..OUT_7), not the old inBits^2 = 9. Address 7 (111b) selects OUT_7,
	// and OUT_8 must not exist.
	Circuit c;
	IDType dec = c.newGate("DECODER");
	c.setGateParameter(dec, "INPUT_BITS", "3");
	IDType d0 = makeDriver(c, 1), d1 = makeDriver(c, 1), d2 = makeDriver(c, 1); // IN = 111b = 7
	IDType wIn0 = c.newWire(), wIn1 = c.newWire(), wIn2 = c.newWire();
	c.connectGateOutput(d0, "OUT_0", wIn0);
	c.connectGateOutput(d1, "OUT_0", wIn1);
	c.connectGateOutput(d2, "OUT_0", wIn2);
	c.connectGateInput(dec, "IN_0", wIn0);
	c.connectGateInput(dec, "IN_1", wIn1);
	c.connectGateInput(dec, "IN_2", wIn2);
	IDType wO7 = c.newWire();
	c.connectGateOutput(dec, "OUT_7", wO7);
	stepN(c, 5);
	CHECK(c.getWireState(wO7) == ONE); // address 7 selected on the last real output

	// OUT_8 was the phantom output; connecting to it now fails (no such hotspot).
	IDType wO8 = c.newWire();
	c.connectGateOutput(dec, "OUT_8", wO8);
	stepN(c, 5);
	CHECK(c.getWireState(wO8) == HI_Z); // unconnected: never driven
}

TEST_CASE("PRI_ENCODER outputs the index of the highest set input") {
	Circuit c;
	IDType enc = c.newGate("PRI_ENCODER");
	c.setGateParameter(enc, "INPUT_BITS", "4"); // 4 inputs -> 2 output bits
	// Drive IN = 0100b: only bit 2 set -> encoded index 2 (binary 10), VALID.
	IDType d0 = makeDriver(c, 0), d1 = makeDriver(c, 0), d2 = makeDriver(c, 1), d3 = makeDriver(c, 0);
	IDType wI0 = c.newWire(), wI1 = c.newWire(), wI2 = c.newWire(), wI3 = c.newWire();
	c.connectGateOutput(d0, "OUT_0", wI0);
	c.connectGateOutput(d1, "OUT_0", wI1);
	c.connectGateOutput(d2, "OUT_0", wI2);
	c.connectGateOutput(d3, "OUT_0", wI3);
	c.connectGateInput(enc, "IN_0", wI0);
	c.connectGateInput(enc, "IN_1", wI1);
	c.connectGateInput(enc, "IN_2", wI2);
	c.connectGateInput(enc, "IN_3", wI3);
	IDType wO0 = c.newWire(), wO1 = c.newWire(), wValid = c.newWire();
	c.connectGateOutput(enc, "OUT_0", wO0);
	c.connectGateOutput(enc, "OUT_1", wO1);
	c.connectGateOutput(enc, "VALID", wValid);
	stepN(c, 5);
	CHECK(c.getWireState(wO0) == ZERO); // index 2 = binary 10
	CHECK(c.getWireState(wO1) == ONE);
	CHECK(c.getWireState(wValid) == ONE);
}

TEST_CASE("REGISTER loads its input bus on a clock edge") {
	Circuit c;
	IDType reg = c.newGate("REGISTER");
	c.setGateParameter(reg, "INPUT_BITS", "4");

	// Control lines: load=1, clear=0, set=0, count/shift disabled, clock enabled.
	auto drive = [&](const char *pin, int v) {
		IDType d = makeDriver(c, v), w = c.newWire();
		c.connectGateOutput(d, "OUT_0", w);
		c.connectGateInput(reg, pin, w);
	};
	drive("clear", 0);
	drive("set", 0);
	drive("load", 1);
	drive("count_enable", 0);
	drive("shift_enable", 0);
	drive("clock_enable", 1);

	// Data input bus IN = 0101b = 5.
	int data[4] = {1, 0, 1, 0};
	for (int i = 0; i < 4; i++) {
		IDType d = makeDriver(c, data[i]), w = c.newWire();
		c.connectGateOutput(d, "OUT_0", w);
		c.connectGateInput(reg, "IN_" + std::to_string(i), w);
	}

	// A clock to drive the load edge.
	IDType clk = c.newGate("CLOCK");
	c.setGateParameter(clk, "HALF_CYCLE", "1");
	IDType wClk = c.newWire();
	c.connectGateOutput(clk, "CLK", wClk);
	c.connectGateInput(reg, "clock", wClk);

	IDType wO0 = c.newWire(), wO1 = c.newWire(), wO2 = c.newWire(), wO3 = c.newWire();
	c.connectGateOutput(reg, "OUT_0", wO0);
	c.connectGateOutput(reg, "OUT_1", wO1);
	c.connectGateOutput(reg, "OUT_2", wO2);
	c.connectGateOutput(reg, "OUT_3", wO3);

	stepN(c, 12);
	CHECK(c.getWireState(wO0) == ONE);  // loaded value 0101b = 5
	CHECK(c.getWireState(wO1) == ZERO);
	CHECK(c.getWireState(wO2) == ONE);
	CHECK(c.getWireState(wO3) == ZERO);
}

TEST_CASE("RAM stores a value on write and reads it back") {
	Circuit c;
	IDType ram = c.newGate("RAM");
	c.setGateParameter(ram, "ADDRESS_BITS", "2");
	c.setGateParameter(ram, "DATA_BITS", "4");

	// ADDRESS = 01b = 1.
	IDType wEnable; // driver id for write_enable, toggled between phases
	{
		IDType da0 = makeDriver(c, 1), da1 = makeDriver(c, 0);
		IDType wa0 = c.newWire(), wa1 = c.newWire();
		c.connectGateOutput(da0, "OUT_0", wa0);
		c.connectGateOutput(da1, "OUT_0", wa1);
		c.connectGateInput(ram, "ADDRESS_0", wa0);
		c.connectGateInput(ram, "ADDRESS_1", wa1);
	}
	// DATA_IN = 1010b = 10.
	int data[4] = {0, 1, 0, 1};
	for (int i = 0; i < 4; i++) {
		IDType d = makeDriver(c, data[i]), w = c.newWire();
		c.connectGateOutput(d, "OUT_0", w);
		c.connectGateInput(ram, "DATA_IN_" + std::to_string(i), w);
	}
	// write_enable starts high (write mode).
	wEnable = makeDriver(c, 1);
	{
		IDType w = c.newWire();
		c.connectGateOutput(wEnable, "OUT_0", w);
		c.connectGateInput(ram, "write_enable", w);
	}
	// write_clock from a CLOCK to produce rising edges.
	IDType clk = c.newGate("CLOCK");
	c.setGateParameter(clk, "HALF_CYCLE", "1");
	IDType wClk = c.newWire();
	c.connectGateOutput(clk, "CLK", wClk);
	c.connectGateInput(ram, "write_clock", wClk);

	IDType o0 = c.newWire(), o1 = c.newWire(), o2 = c.newWire(), o3 = c.newWire();
	c.connectGateOutput(ram, "DATA_OUT_0", o0);
	c.connectGateOutput(ram, "DATA_OUT_1", o1);
	c.connectGateOutput(ram, "DATA_OUT_2", o2);
	c.connectGateOutput(ram, "DATA_OUT_3", o3);

	stepN(c, 8); // write phase: value latched on a write_clock rising edge

	// Switch to read mode: DATA_OUT should present memory[1] = 1010b.
	c.setGateParameter(wEnable, "OUTPUT_NUM", "0");
	stepN(c, 8);
	CHECK(c.getWireState(o0) == ZERO);
	CHECK(c.getWireState(o1) == ONE);
	CHECK(c.getWireState(o2) == ZERO);
	CHECK(c.getWireState(o3) == ONE);
}

TEST_CASE("PULSE holds its output high for a bounded number of steps") {
	Circuit c;
	IDType p = c.newGate("PULSE");
	IDType w = c.newWire();
	c.connectGateOutput(p, "OUT_0", w);
	c.setGateParameter(p, "PULSE", "3");

	int highCount = 0;
	bool returnedLow = false;
	for (int i = 0; i < 8; i++) {
		ID_SET<IDType> ch;
		c.step(&ch);
		StateType s = c.getWireState(w);
		if (s == ONE) highCount++;
		if (s == ZERO && highCount > 0) returnedLow = true;
	}
	CHECK(highCount >= 1);   // the pulse went high
	CHECK(highCount <= 4);   // for ~3 steps (allow +/-1 for propagation), not forever
	CHECK(returnedLow);      // and then dropped back low
}

TEST_CASE("NODE bridges its connected wires into one net") {
	// A NODE splices all its inputs onto one always-connected junction, so a
	// value driven onto one input wire appears on the others.
	Circuit c;
	IDType node = c.newGate("NODE");
	IDType drv = makeDriver(c, 1);
	IDType wA = c.newWire(), wB = c.newWire();
	c.connectGateOutput(drv, "OUT_0", wA);
	c.connectGateInput(node, "N_in0", wA); // driven
	c.connectGateInput(node, "N_in1", wB); // undriven -> bridged to wA
	stepN(c, 5);
	CHECK(c.getWireState(wB) == ONE);
}

TEST_CASE("TGATE bridges its two data wires only when the control is high") {
	// Gate_T connects T_in/T_in2 to a junction that is enabled only while
	// T_ctrl == ONE, so the two data wires are bridged conditionally.
	auto bridgedState = [](int ctrl) {
		Circuit c;
		IDType tg = c.newGate("TGATE");
		IDType drv = makeDriver(c, 1);
		IDType dctrl = makeDriver(c, ctrl);
		IDType wA = c.newWire(), wB = c.newWire(), wC = c.newWire();
		c.connectGateOutput(drv, "OUT_0", wA);
		c.connectGateOutput(dctrl, "OUT_0", wC);
		c.connectGateInput(tg, "T_in", wA);
		c.connectGateInput(tg, "T_in2", wB);
		c.connectGateInput(tg, "T_ctrl", wC);
		stepN(c, 5);
		return c.getWireState(wB);
	};
	CHECK(bridgedState(1) == ONE);  // control high -> wB sees the driven value
	CHECK(bridgedState(0) == HI_Z); // control low  -> open, wB floats
}

// TODO(behavioral coverage): FROM/TO (Gate_JUNCTION, named cross-circuit
// junctions) and BUS_END (per-line bus junctions) remain untested -- both
// need matched-pair / named-junction setups and have undeclared input pins,
// so they are awkward to drive in isolation. (BUFFER already exercises
// Gate_PASS, and TGATE is the "T" gate, so those are covered.)
// Also worth adding: REGISTER count/shift/clear ops, JKFF SYNC_SET/SYNC_CLEAR
// and toggle (J=K=1) behavior.

TEST_CASE("System time advances one unit per step") {
	Circuit c;
	CHECK(c.getSystemTime() == 0);
	stepN(c, 1);
	CHECK(c.getSystemTime() == 1);
	stepN(c, 9);
	CHECK(c.getSystemTime() == 10);
}

TEST_CASE("Deleting a gate mid-circuit is safe and stops driving") {
	Circuit c;
	IDType gate = c.newGate("AND");
	c.setGateParameter(gate, "INPUT_BITS", "2");
	IDType d0 = makeDriver(c, 1), d1 = makeDriver(c, 1);
	IDType w0 = c.newWire(), w1 = c.newWire(), wOut = c.newWire();
	c.connectGateOutput(d0, "OUT_0", w0);
	c.connectGateOutput(d1, "OUT_0", w1);
	c.connectGateInput(gate, "IN_0", w0);
	c.connectGateInput(gate, "IN_1", w1);
	c.connectGateOutput(gate, "OUT", wOut);
	stepN(c, 5);
	REQUIRE(c.getWireState(wOut) == ONE);

	c.deleteGate(gate);
	stepN(c, 5);
	CHECK(c.getWireState(wOut) == HI_Z); // no driver left on the output wire
}

TEST_CASE("Every declared gate param round-trips through set/getParameter") {
	// Each gate's paramSchema() must match its real setParameter/getParameter.
	Circuit c;
	for (const auto &entry : gateRegistry()) {
		GATE_PTR g = entry.second.create(&c);
		for (const ParamDescriptor &p : g->paramSchema()) {
			std::string v;
			switch (p.kind) {
			case ParamKind::INT:  v = "5"; break;
			case ParamKind::BITS: v = "3"; break;
			case ParamKind::BOOL: v = "true"; break;
			default:              v = "x"; break;
			}
			g->setParameter(p.name, v);
			INFO("gate " << entry.first << " param " << p.name);
			CHECK(g->getParameter(p.name) == v);
		}
	}
}
