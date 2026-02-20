// Main → Worker messages
export type MainToWorkerMessage =
  | { type: "init" }
  | { type: "fullSync"; gates: FullSyncGate[]; wires: FullSyncWire[]; connections: FullSyncConnection[] }
  | { type: "addGate"; id: string; logicType: string; params: Record<string, string>; invertedInputs?: string[]; invertedOutputs?: string[] }
  | { type: "removeGate"; id: string }
  | { type: "addWire"; id: string }
  | { type: "removeWire"; id: string }
  | { type: "connect"; gateId: string; pinName: string; pinDirection: "input" | "output"; wireId: string }
  | { type: "disconnect"; gateId: string; pinName: string; pinDirection: "input" | "output" }
  | { type: "setParam"; gateId: string; paramName: string; value: string }
  | { type: "step"; count: number }
  | { type: "setRunning"; running: boolean; stepsPerFrame: number; gen: number };

// Worker → Main messages
export type WorkerToMainMessage =
  | { type: "ready" }
  | { type: "wireStates"; states: Array<{ id: string; state: number }>; gen: number }
  | { type: "time"; time: number; gen: number }
  | { type: "error"; message: string };

export interface FullSyncGate {
  id: string;
  logicType: string;
  params: Record<string, string>;
  invertedInputs?: string[];
  invertedOutputs?: string[];
}

export interface FullSyncWire {
  id: string;
}

export interface FullSyncConnection {
  gateId: string;
  pinName: string;
  pinDirection: "input" | "output";
  wireId: string;
}
