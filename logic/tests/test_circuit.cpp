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

// TODO(behavioral coverage): the following shipping gates still lack C++
// simulation tests. Each needs its pin names + expected values worked out
// (the ADDER above shows the multi-bit/bus pattern):
//   COMPARE, PRI_ENCODER, DECODER, REGISTER, RAM, PULSE, TGATE, NODE,
//   FROM/TO, BUS_END, PASS, T
// Also worth adding: multi-bit BUFFER/bus wiring, JKFF SYNC_SET/SYNC_CLEAR
// and toggle (J=K=1) behavior, and RAM read-after-write.

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
