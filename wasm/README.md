# @cedarville/cedarlogic-engine

CedarLogic's digital circuit simulation engine compiled to WebAssembly. Run logic simulations in the browser or Node.js — ~385KB total.

## Install

```bash
npm install @cedarville/cedarlogic-engine
```

## Quick Start

```ts
import CedarLogic from "@cedarville/cedarlogic-engine";

const engine = await CedarLogic();
const circuit = new engine.Circuit();

// Create an AND gate with 2 inputs
const and = circuit.newGateAuto("AND");
circuit.setGateParameter(and, "INPUT_BITS", "2");

// Drive both inputs HIGH
const drv0 = circuit.newGateAuto("DRIVER");
circuit.setGateParameter(drv0, "OUTPUT_BITS", "1");
circuit.setGateParameter(drv0, "OUTPUT_NUM", "1");

const drv1 = circuit.newGateAuto("DRIVER");
circuit.setGateParameter(drv1, "OUTPUT_BITS", "1");
circuit.setGateParameter(drv1, "OUTPUT_NUM", "1");

// Wire it up
const w0 = circuit.newWireAuto();
const w1 = circuit.newWireAuto();
const wOut = circuit.newWireAuto();

circuit.connectGateOutput(drv0, "OUT_0", w0);
circuit.connectGateOutput(drv1, "OUT_0", w1);
circuit.connectGateInput(and, "IN_0", w0);
circuit.connectGateInput(and, "IN_1", w1);
circuit.connectGateOutput(and, "OUT", wOut);

// Simulate
circuit.stepN(5);
console.log(circuit.getWireState(wOut)); // 1 (ONE)

circuit.delete(); // free C++ memory
```

## Wire States

| Constant | Value | Description |
|----------|-------|-------------|
| `ZERO` | `0` | Logic low |
| `ONE` | `1` | Logic high |
| `HI_Z` | `2` | High impedance |
| `CONFLICT` | `3` | Driver conflict |
| `UNKNOWN` | `4` | Uninitialized |

## Gate Types

**Combinational:** `AND`, `OR`, `XOR`, `EQUIVALENCE` (XNOR), `BUFFER`

**Input/Output:** `DRIVER`, `CLOCK`, `PULSE`

**Mux/Decoder:** `MUX`, `DECODER`, `PRI_ENCODER`

**Arithmetic:** `ADDER`, `COMPARE`

**Sequential:** `JKFF`, `REGISTER`

**Memory:** `RAM`

**Connectivity:** `FROM`, `TO`, `TGATE`, `NODE`, `BUS_END`

## API

```ts
// Creation
circuit.newGateAuto(type: GateType): number
circuit.newWireAuto(): number
circuit.deleteGate(id: number): void
circuit.deleteWire(id: number): void

// Connections
circuit.connectGateInput(gateID, inputName, wireID): number
circuit.connectGateOutput(gateID, outputName, wireID): number
circuit.disconnectGateInput(gateID, inputName): void
circuit.disconnectGateOutput(gateID, outputName): void

// Parameters
circuit.setGateParameter(gateID, name, value): void
circuit.getGateParameter(gateID, name): string

// Simulation
circuit.step(): StepResult        // { changedWires: [{id, state}], time }
circuit.stepN(n): StepResult      // batch step
circuit.getWireState(wireID): WireState
circuit.getSystemTime(): number
circuit.delete(): void            // must call when done
```

Full API docs and pin reference: [Wiki](https://github.com/taciturnaxolotl/cedarlogic/wiki/WebAssembly)

## License

MIT
