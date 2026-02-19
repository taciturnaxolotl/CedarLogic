/** Wire state values. */
export const ZERO = 0;
export const ONE = 1;
export const HI_Z = 2;
export const CONFLICT = 3;
export const UNKNOWN = 4;

export type WireState =
  | typeof ZERO
  | typeof ONE
  | typeof HI_Z
  | typeof CONFLICT
  | typeof UNKNOWN;

/** Gate type strings accepted by Circuit.newGate / newGateAuto. */
export type GateType =
  | "AND"
  | "OR"
  | "XOR"
  | "EQUIVALENCE"
  | "BUFFER"
  | "CLOCK"
  | "PULSE"
  | "DRIVER"
  | "MUX"
  | "DECODER"
  | "PRI_ENCODER"
  | "ADDER"
  | "COMPARE"
  | "JKFF"
  | "REGISTER"
  | "RAM"
  | "FROM"
  | "TO"
  | "TGATE"
  | "NODE"
  | "BUS_END"
  | "Pauseulator";

export interface WireChange {
  id: number;
  state: WireState;
}

export interface StepResult {
  changedWires: WireChange[];
  time: number;
}

export interface Circuit {
  /** Create a gate with a specific ID. */
  newGate(type: GateType, gateID: number): number;
  /** Create a gate with an auto-assigned ID. */
  newGateAuto(type: GateType): number;
  /** Create a wire with a specific ID. */
  newWire(wireID: number): number;
  /** Create a wire with an auto-assigned ID. */
  newWireAuto(): number;

  deleteGate(gateID: number): void;
  deleteWire(wireID: number): void;

  /** Connect a wire to a gate's named input pin. */
  connectGateInput(gateID: number, inputID: string, wireID: number): number;
  /** Connect a gate's named output pin to a wire. */
  connectGateOutput(gateID: number, outputID: string, wireID: number): number;
  disconnectGateInput(gateID: number, inputID: string): void;
  disconnectGateOutput(gateID: number, outputID: string): void;

  setGateParameter(gateID: number, paramName: string, value: string): void;
  getGateParameter(gateID: number, paramName: string): string;
  setGateInputParameter(gateID: number, inputID: string, paramName: string, value: string): void;
  setGateOutputParameter(gateID: number, outputID: string, paramName: string, value: string): void;

  getWireState(wireID: number): WireState;

  /** Advance simulation by one step. Returns changed wires and current time. */
  step(): StepResult;
  /** Advance simulation by n steps. Returns all wires that changed and final time. */
  stepN(n: number): StepResult;
  /** Re-evaluate gates without advancing time (e.g. after parameter changes). */
  stepOnlyGates(): void;

  getSystemTime(): number;
  destroyAllEvents(): void;

  /** Free the C++ Circuit object. Must be called when done. */
  delete(): void;
}

export interface CedarLogicModule {
  ZERO: typeof ZERO;
  ONE: typeof ONE;
  HI_Z: typeof HI_Z;
  CONFLICT: typeof CONFLICT;
  UNKNOWN: typeof UNKNOWN;

  Circuit: { new (): Circuit };
}

/** Initialize the WASM module. Returns a promise that resolves when ready. */
declare function CedarLogic(): Promise<CedarLogicModule>;
export default CedarLogic;
