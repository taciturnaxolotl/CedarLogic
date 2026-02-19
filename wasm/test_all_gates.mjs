// Comprehensive test of all gate types in the WASM module
import { createRequire } from 'module';
import { dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const CedarLogic = require('./build/cedarlogic.js');

let passed = 0, failed = 0;

function assert(condition, msg) {
	if (condition) {
		console.log(`  PASS: ${msg}`);
		passed++;
	} else {
		console.log(`  FAIL: ${msg}`);
		failed++;
	}
}

function stateStr(Module, state) {
	return ['ZERO', 'ONE', 'HI_Z', 'CONFLICT', 'UNKNOWN'][state] ?? `?${state}`;
}

// Helper: create a driver gate outputting a 1-bit value
function makeDriver(c, value) {
	const drv = c.newGateAuto('DRIVER');
	c.setGateParameter(drv, 'OUTPUT_BITS', '1');
	c.setGateParameter(drv, 'OUTPUT_NUM', String(value));
	return drv;
}

async function main() {
	const Module = await CedarLogic();
	const { ZERO, ONE, HI_Z, CONFLICT, UNKNOWN } = Module;

	// --- AND gate ---
	console.log('\n--- AND gate ---');
	for (const [a, b, expected] of [[1,1,ONE],[1,0,ZERO],[0,1,ZERO],[0,0,ZERO]]) {
		const c = new Module.Circuit();
		const gate = c.newGateAuto('AND'); c.setGateParameter(gate, 'INPUT_BITS', '2');
		const d0 = makeDriver(c, a), d1 = makeDriver(c, b);
		const w0 = c.newWireAuto(), w1 = c.newWireAuto(), wOut = c.newWireAuto();
		c.connectGateOutput(d0, 'OUT_0', w0); c.connectGateOutput(d1, 'OUT_0', w1);
		c.connectGateInput(gate, 'IN_0', w0); c.connectGateInput(gate, 'IN_1', w1);
		c.connectGateOutput(gate, 'OUT', wOut);
		c.stepN(5);
		assert(c.getWireState(wOut) === expected, `AND(${a},${b}) = ${stateStr(Module, expected)}`);
		c.delete();
	}

	// --- OR gate ---
	console.log('\n--- OR gate ---');
	for (const [a, b, expected] of [[1,1,ONE],[1,0,ONE],[0,1,ONE],[0,0,ZERO]]) {
		const c = new Module.Circuit();
		const gate = c.newGateAuto('OR'); c.setGateParameter(gate, 'INPUT_BITS', '2');
		const d0 = makeDriver(c, a), d1 = makeDriver(c, b);
		const w0 = c.newWireAuto(), w1 = c.newWireAuto(), wOut = c.newWireAuto();
		c.connectGateOutput(d0, 'OUT_0', w0); c.connectGateOutput(d1, 'OUT_0', w1);
		c.connectGateInput(gate, 'IN_0', w0); c.connectGateInput(gate, 'IN_1', w1);
		c.connectGateOutput(gate, 'OUT', wOut);
		c.stepN(5);
		assert(c.getWireState(wOut) === expected, `OR(${a},${b}) = ${stateStr(Module, expected)}`);
		c.delete();
	}

	// --- XOR gate ---
	console.log('\n--- XOR gate ---');
	for (const [a, b, expected] of [[1,1,ZERO],[1,0,ONE],[0,1,ONE],[0,0,ZERO]]) {
		const c = new Module.Circuit();
		const gate = c.newGateAuto('XOR'); c.setGateParameter(gate, 'INPUT_BITS', '2');
		const d0 = makeDriver(c, a), d1 = makeDriver(c, b);
		const w0 = c.newWireAuto(), w1 = c.newWireAuto(), wOut = c.newWireAuto();
		c.connectGateOutput(d0, 'OUT_0', w0); c.connectGateOutput(d1, 'OUT_0', w1);
		c.connectGateInput(gate, 'IN_0', w0); c.connectGateInput(gate, 'IN_1', w1);
		c.connectGateOutput(gate, 'OUT', wOut);
		c.stepN(5);
		assert(c.getWireState(wOut) === expected, `XOR(${a},${b}) = ${stateStr(Module, expected)}`);
		c.delete();
	}

	// --- EQUIVALENCE (XNOR) gate ---
	console.log('\n--- EQUIVALENCE (XNOR) gate ---');
	for (const [a, b, expected] of [[1,1,ONE],[1,0,ZERO],[0,1,ZERO],[0,0,ONE]]) {
		const c = new Module.Circuit();
		const gate = c.newGateAuto('EQUIVALENCE'); c.setGateParameter(gate, 'INPUT_BITS', '2');
		const d0 = makeDriver(c, a), d1 = makeDriver(c, b);
		const w0 = c.newWireAuto(), w1 = c.newWireAuto(), wOut = c.newWireAuto();
		c.connectGateOutput(d0, 'OUT_0', w0); c.connectGateOutput(d1, 'OUT_0', w1);
		c.connectGateInput(gate, 'IN_0', w0); c.connectGateInput(gate, 'IN_1', w1);
		c.connectGateOutput(gate, 'OUT', wOut);
		c.stepN(5);
		assert(c.getWireState(wOut) === expected, `XNOR(${a},${b}) = ${stateStr(Module, expected)}`);
		c.delete();
	}

	// --- BUFFER (PASS) gate ---
	console.log('\n--- BUFFER gate ---');
	for (const [val, expected] of [[1, ONE], [0, ZERO]]) {
		const c = new Module.Circuit();
		const gate = c.newGateAuto('BUFFER'); c.setGateParameter(gate, 'INPUT_BITS', '1');
		const drv = makeDriver(c, val);
		const wIn = c.newWireAuto(), wOut = c.newWireAuto();
		c.connectGateOutput(drv, 'OUT_0', wIn);
		c.connectGateInput(gate, 'IN_0', wIn);
		c.connectGateOutput(gate, 'OUT_0', wOut);
		c.stepN(5);
		assert(c.getWireState(wOut) === expected, `BUFFER(${val}) = ${stateStr(Module, expected)}`);
		c.delete();
	}

	// --- CLOCK gate ---
	console.log('\n--- CLOCK gate ---');
	{
		const c = new Module.Circuit();
		const clk = c.newGateAuto('CLOCK');
		c.setGateParameter(clk, 'HALF_CYCLE', '2');
		const wOut = c.newWireAuto();
		c.connectGateOutput(clk, 'CLK', wOut);
		const states = [];
		for (let i = 0; i < 10; i++) { c.step(); states.push(c.getWireState(wOut)); }
		let toggles = 0;
		for (let i = 1; i < states.length; i++) if (states[i] !== states[i-1]) toggles++;
		assert(toggles >= 2, `Clock toggled ${toggles} times in 10 steps (states: ${states})`);
		c.delete();
	}

	// --- JK Flip-Flop ---
	console.log('\n--- JK Flip-Flop ---');
	{
		const c = new Module.Circuit();
		const jkff = c.newGateAuto('JKFF');
		// Create clock for it (output pin is 'CLK')
		const clk = c.newGateAuto('CLOCK');
		c.setGateParameter(clk, 'HALF_CYCLE', '1');
		const wClk = c.newWireAuto();
		c.connectGateOutput(clk, 'CLK', wClk);
		c.connectGateInput(jkff, 'clock', wClk); // JKFF uses lowercase 'clock'
		// J=1, K=0 → should set Q to 1
		const drvJ = makeDriver(c, 1), drvK = makeDriver(c, 0);
		const wJ = c.newWireAuto(), wK = c.newWireAuto(), wQ = c.newWireAuto();
		c.connectGateOutput(drvJ, 'OUT_0', wJ);
		c.connectGateOutput(drvK, 'OUT_0', wK);
		c.connectGateInput(jkff, 'J', wJ);
		c.connectGateInput(jkff, 'K', wK);
		c.connectGateOutput(jkff, 'Q', wQ);
		c.stepN(10);
		assert(c.getWireState(wQ) === ONE, `JKFF J=1,K=0 → Q=ONE (got ${stateStr(Module, c.getWireState(wQ))})`);
		c.delete();
	}

	// --- MUX ---
	console.log('\n--- MUX gate ---');
	{
		const c = new Module.Circuit();
		const mux = c.newGateAuto('MUX');
		c.setGateParameter(mux, 'INPUT_BITS', '2');
		// 2:1 MUX: IN_0, IN_1 are data inputs; SEL_0 is select; OUT is output
		const drvA = makeDriver(c, 0); // IN_0 = 0
		const drvB = makeDriver(c, 1); // IN_1 = 1
		const drvSel = makeDriver(c, 1); // SEL_0 = 1 → select IN_1
		const wA = c.newWireAuto(), wB = c.newWireAuto(), wSel = c.newWireAuto(), wOut = c.newWireAuto();
		c.connectGateOutput(drvA, 'OUT_0', wA);
		c.connectGateOutput(drvB, 'OUT_0', wB);
		c.connectGateOutput(drvSel, 'OUT_0', wSel);
		c.connectGateInput(mux, 'IN_0', wA);
		c.connectGateInput(mux, 'IN_1', wB);
		c.connectGateInput(mux, 'SEL_0', wSel);
		c.connectGateOutput(mux, 'OUT', wOut);
		c.stepN(5);
		assert(c.getWireState(wOut) === ONE, `MUX sel=1 → OUT=IN_1=ONE (got ${stateStr(Module, c.getWireState(wOut))})`);
		c.delete();
	}

	// --- Step return value ---
	console.log('\n--- step() API ---');
	{
		const c = new Module.Circuit();
		const clk = c.newGateAuto('CLOCK');
		c.setGateParameter(clk, 'HALF_CYCLE', '1');
		const wOut = c.newWireAuto();
		c.connectGateOutput(clk, 'CLK', wOut);
		const result = c.step();
		assert(typeof result.time === 'number', `step() returns time=${result.time}`);
		assert(Array.isArray(result.changedWires), `step() returns changedWires array`);
		c.delete();
	}

	// --- getSystemTime ---
	console.log('\n--- getSystemTime ---');
	{
		const c = new Module.Circuit();
		assert(c.getSystemTime() === 0, 'Initial time is 0');
		c.step();
		assert(c.getSystemTime() === 1, 'After 1 step, time is 1');
		c.stepN(9);
		assert(c.getSystemTime() === 10, 'After 10 total steps, time is 10');
		c.delete();
	}

	console.log(`\n========================================`);
	console.log(`Results: ${passed} passed, ${failed} failed`);
	console.log(`========================================`);
	process.exit(failed > 0 ? 1 : 0);
}

main().catch(e => { console.error(e); process.exit(1); });
