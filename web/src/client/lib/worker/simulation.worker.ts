import type { MainToWorkerMessage, WorkerToMainMessage } from "./protocol";

// Dynamic import of WASM module
let circuit: any = null;
let cedarModule: any = null;
let running = false;
let stepsPerFrame = 5;

// Maps between string IDs and numeric IDs for the WASM engine
const gateIdMap = new Map<string, number>();
const wireIdMap = new Map<string, number>(); // Yjs wireId → WASM wire number (merged wires share a number)
const wireMergeMap = new Map<string, string>(); // merged wireId → canonical wireId (for tracking)
let nextGateId = 1;
let nextWireId = 1;

function post(msg: WorkerToMainMessage) {
  self.postMessage(msg);
}

async function init() {
  try {
    const CedarLogic = (await import("@cedarville/cedarlogic-engine")).default as any;
    cedarModule = await CedarLogic({
      locateFile(path: string) {
        if (path.endsWith(".wasm")) {
          return "/" + path;
        }
        return path;
      },
    });
    circuit = new cedarModule.Circuit();
    post({ type: "ready" });
  } catch (e: any) {
    post({ type: "error", message: `Failed to init WASM: ${e.message}` });
  }
}

function getOrCreateGateId(id: string): number {
  if (gateIdMap.has(id)) return gateIdMap.get(id)!;
  const numId = nextGateId++;
  gateIdMap.set(id, numId);
  return numId;
}

function getOrCreateWireId(id: string): number {
  if (wireIdMap.has(id)) return wireIdMap.get(id)!;
  const numId = nextWireId++;
  wireIdMap.set(id, numId);
  return numId;
}

function fullSync(msg: Extract<MainToWorkerMessage, { type: "fullSync" }>) {
  if (!circuit) return;

  // Destroy and recreate circuit
  circuit.delete();
  circuit = new cedarModule.Circuit();
  gateIdMap.clear();
  wireIdMap.clear();
  wireMergeMap.clear();
  nextGateId = 1;
  nextWireId = 1;

  // Add gates
  for (const gate of msg.gates) {
    const numId = getOrCreateGateId(gate.id);
    try {
      circuit.newGate(gate.logicType, numId);
      for (const [param, value] of Object.entries(gate.params)) {
        circuit.setGateParameter(numId, param, value);
      }
      if (gate.invertedInputs) {
        for (const pinName of gate.invertedInputs) {
          circuit.setGateInputParameter(numId, pinName, "INVERTED", "TRUE");
        }
      }
      if (gate.invertedOutputs) {
        for (const pinName of gate.invertedOutputs) {
          circuit.setGateOutputParameter(numId, pinName, "INVERTED", "TRUE");
        }
      }
    } catch (e: any) {
      console.warn(`Failed to create gate ${gate.id}: ${e.message}`);
    }
  }

  // Build merge groups: wires sharing the same gate pin must use the same
  // WASM wire so the signal fans out correctly (the engine supports only one
  // wire per pin).
  // pinKey → list of Yjs wireIds sharing that pin
  const pinToWires = new Map<string, string[]>();
  for (const conn of msg.connections) {
    const pinKey = `${conn.gateId}:${conn.pinName}:${conn.pinDirection}`;
    let arr = pinToWires.get(pinKey);
    if (!arr) { arr = []; pinToWires.set(pinKey, arr); }
    if (!arr.includes(conn.wireId)) arr.push(conn.wireId);
  }

  // Union-Find to merge wire groups transitively (a wire might share pins
  // with different wires on different gates, forming a larger equivalence set).
  const parent = new Map<string, string>();
  function find(a: string): string {
    while (parent.get(a) !== a) {
      const p = parent.get(a)!;
      parent.set(a, parent.get(p)!); // path compression
      a = p;
    }
    return a;
  }
  function union(a: string, b: string) {
    const ra = find(a), rb = find(b);
    if (ra !== rb) parent.set(ra, rb);
  }

  // Initialize each wire as its own parent
  for (const wire of msg.wires) {
    parent.set(wire.id, wire.id);
  }
  // Also init wires referenced in connections but maybe not in msg.wires
  for (const conn of msg.connections) {
    if (!parent.has(conn.wireId)) parent.set(conn.wireId, conn.wireId);
  }

  // Merge wires that share a pin
  for (const wireIds of pinToWires.values()) {
    for (let i = 1; i < wireIds.length; i++) {
      union(wireIds[0], wireIds[i]);
    }
  }

  // Build canonical wire ID per group and create a single WASM wire for each group
  const groupWasmId = new Map<string, number>(); // group root → WASM wire ID
  for (const wire of msg.wires) {
    const root = find(wire.id);
    if (!groupWasmId.has(root)) {
      const numId = nextWireId++;
      groupWasmId.set(root, numId);
      try {
        circuit.newWire(numId);
      } catch (e: any) {
        console.warn(`Failed to create wire ${wire.id}: ${e.message}`);
      }
    }
    const wasmId = groupWasmId.get(root)!;
    wireIdMap.set(wire.id, wasmId);
    if (root !== wire.id) {
      wireMergeMap.set(wire.id, root);
    }
  }

  // Add connections — each pin is connected exactly once (to the merged WASM wire)
  const connectedPins = new Set<string>();
  for (const conn of msg.connections) {
    const gateNum = gateIdMap.get(conn.gateId);
    const wireNum = wireIdMap.get(conn.wireId);
    if (gateNum === undefined || wireNum === undefined) continue;
    // Only connect each pin once (all wires on this pin share the same WASM wire)
    const pinKey = `${gateNum}:${conn.pinName}:${conn.pinDirection}`;
    if (connectedPins.has(pinKey)) continue;
    connectedPins.add(pinKey);
    try {
      if (conn.pinDirection === "input") {
        circuit.connectGateInput(gateNum, conn.pinName, wireNum);
      } else {
        circuit.connectGateOutput(gateNum, conn.pinName, wireNum);
      }
    } catch (e: any) {
      console.warn(`Failed to connect: ${e.message}`);
    }
  }

  // Settle and report all wire states
  stepAndReport(5);
  lastWireState.clear();
  reportAllWireStates();
}

function stepAndReport(count: number) {
  if (!circuit) return;
  try {
    const result = circuit.stepN(count);
    const states = result.changedWires.map((w: any) => {
      // Find string ID for this numeric wire ID
      let stringId = "";
      for (const [sid, nid] of wireIdMap) {
        if (nid === w.id) {
          stringId = sid;
          break;
        }
      }
      return { id: stringId, state: w.state };
    });
    if (states.length > 0) {
      post({ type: "wireStates", states });
    }
    post({ type: "time", time: result.time });
  } catch (e: any) {
    post({ type: "error", message: `Step error: ${e.message}` });
  }
}

/** Report ALL wire states (not just changed). Used after fullSync to init colors.
 *  Reports state for every Yjs wireId, including merged wires that share a WASM wire. */
function reportAllWireStates() {
  if (!circuit) return;
  const states: Array<{ id: string; state: number }> = [];
  for (const [stringId, numId] of wireIdMap) {
    try {
      states.push({ id: stringId, state: circuit.getWireState(numId) });
    } catch {
      // skip
    }
  }
  if (states.length > 0) {
    post({ type: "wireStates", states });
  }
}

// Cache of last-known wire states so we can diff and only post actual changes
const lastWireState = new Map<string, number>();

/** Poll all wire states and post only those that changed since last poll. */
function reportChangedWireStates() {
  if (!circuit) return;
  const changed: Array<{ id: string; state: number }> = [];
  for (const [stringId, numId] of wireIdMap) {
    try {
      const state = circuit.getWireState(numId);
      if (lastWireState.get(stringId) !== state) {
        lastWireState.set(stringId, state);
        changed.push({ id: stringId, state });
      }
    } catch {
      // skip
    }
  }
  if (changed.length > 0) {
    post({ type: "wireStates", states: changed });
  }
}

let runTimer: ReturnType<typeof setTimeout> | null = null;

function runLoop() {
  if (!running) return;
  try {
    const result = circuit?.stepN(1);
    if (result) post({ type: "time", time: result.time });
  } catch (e: any) {
    post({ type: "error", message: `Step error: ${e.message}` });
  }
  reportChangedWireStates();
  const interval = Math.max(1, Math.round(1000 / stepsPerFrame));
  runTimer = setTimeout(runLoop, interval);
}

self.onmessage = (e: MessageEvent<MainToWorkerMessage>) => {
  const msg = e.data;

  switch (msg.type) {
    case "init":
      init();
      break;

    case "fullSync":
      fullSync(msg);
      break;

    case "addGate": {
      if (!circuit) break;
      const numId = getOrCreateGateId(msg.id);
      try {
        circuit.newGate(msg.logicType, numId);
        for (const [param, value] of Object.entries(msg.params)) {
          circuit.setGateParameter(numId, param, value);
        }
        if (msg.invertedInputs) {
          for (const pinName of msg.invertedInputs) {
            circuit.setGateInputParameter(numId, pinName, "INVERTED", "TRUE");
          }
        }
        if (msg.invertedOutputs) {
          for (const pinName of msg.invertedOutputs) {
            circuit.setGateOutputParameter(numId, pinName, "INVERTED", "TRUE");
          }
        }
      } catch (e: any) {
        post({ type: "error", message: `addGate: ${e.message}` });
      }
      break;
    }

    case "removeGate": {
      const numId = gateIdMap.get(msg.id);
      if (numId !== undefined && circuit) {
        try {
          circuit.deleteGate(numId);
        } catch {}
        gateIdMap.delete(msg.id);
      }
      break;
    }

    case "addWire": {
      if (!circuit) break;
      const numId = getOrCreateWireId(msg.id);
      try {
        circuit.newWire(numId);
      } catch (e: any) {
        post({ type: "error", message: `addWire: ${e.message}` });
      }
      break;
    }

    case "removeWire": {
      const numId = wireIdMap.get(msg.id);
      if (numId !== undefined && circuit) {
        try {
          circuit.deleteWire(numId);
        } catch {}
        wireIdMap.delete(msg.id);
      }
      break;
    }

    case "connect": {
      const gateNum = gateIdMap.get(msg.gateId);
      const wireNum = wireIdMap.get(msg.wireId);
      if (gateNum === undefined || wireNum === undefined || !circuit) break;
      try {
        if (msg.pinDirection === "input") {
          circuit.connectGateInput(gateNum, msg.pinName, wireNum);
        } else {
          circuit.connectGateOutput(gateNum, msg.pinName, wireNum);
        }
      } catch (e: any) {
        post({ type: "error", message: `connect: ${e.message}` });
      }
      break;
    }

    case "disconnect": {
      const gateNum = gateIdMap.get(msg.gateId);
      if (gateNum === undefined || !circuit) break;
      try {
        if (msg.pinDirection === "input") {
          circuit.disconnectGateInput(gateNum, msg.pinName);
        } else {
          circuit.disconnectGateOutput(gateNum, msg.pinName);
        }
      } catch (e: any) {
        post({ type: "error", message: `disconnect: ${e.message}` });
      }
      break;
    }

    case "setParam": {
      const gateNum = gateIdMap.get(msg.gateId);
      if (gateNum === undefined || !circuit) break;
      try {
        circuit.setGateParameter(gateNum, msg.paramName, msg.value);
      } catch (e: any) {
        post({ type: "error", message: `setParam: ${e.message}` });
      }
      break;
    }

    case "step":
      // Ignored — runLoop is the sole driver of simulation steps
      break;

    case "setRunning":
      running = msg.running;
      stepsPerFrame = msg.stepsPerFrame;
      // Always clear existing timer to avoid overlapping loops
      if (runTimer) {
        clearTimeout(runTimer);
        runTimer = null;
      }
      if (running) {
        runLoop();
      }
      break;
  }
};
