import type { MainToWorkerMessage, WorkerToMainMessage } from "./protocol";

// Dynamic import of WASM module
let circuit: any = null;
let cedarModule: any = null;
let running = false;
let stepsPerFrame = 5;

// Maps between string IDs and numeric IDs for the WASM engine
const gateIdMap = new Map<string, number>();
const wireIdMap = new Map<string, number>();
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
    } catch (e: any) {
      console.warn(`Failed to create gate ${gate.id}: ${e.message}`);
    }
  }

  // Add wires
  for (const wire of msg.wires) {
    const numId = getOrCreateWireId(wire.id);
    try {
      circuit.newWire(numId);
    } catch (e: any) {
      console.warn(`Failed to create wire ${wire.id}: ${e.message}`);
    }
  }

  // Add connections
  for (const conn of msg.connections) {
    const gateNum = gateIdMap.get(conn.gateId);
    const wireNum = wireIdMap.get(conn.wireId);
    if (gateNum === undefined || wireNum === undefined) continue;
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

  // Settle
  stepAndReport(5);
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

let runTimer: ReturnType<typeof setTimeout> | null = null;

function runLoop() {
  if (!running) return;
  stepAndReport(stepsPerFrame);
  runTimer = setTimeout(runLoop, 16); // ~60Hz
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
        circuit.stepOnlyGates();
      } catch (e: any) {
        post({ type: "error", message: `setParam: ${e.message}` });
      }
      break;
    }

    case "step":
      stepAndReport(msg.count);
      break;

    case "setRunning":
      running = msg.running;
      stepsPerFrame = msg.stepsPerFrame;
      if (running) {
        runLoop();
      } else if (runTimer) {
        clearTimeout(runTimer);
        runTimer = null;
      }
      break;
  }
};
