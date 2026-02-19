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

// ─── Gate Type Definitions ──────────────────────────────────────────

export type GateType = keyof GateInputPins;

/** Enable pins inherited by all gates. */
type EnablePins =
  | "ENABLE_0" | "ENABLE_1" | "ENABLE_2" | "ENABLE_3"
  | "ENABLE_4" | "ENABLE_5" | "ENABLE_6" | "ENABLE_7";

/** Helper for bus pins: IN_0 through IN_7. Covers up to 8-bit buses. */
type BusIn = "IN_0" | "IN_1" | "IN_2" | "IN_3" | "IN_4" | "IN_5" | "IN_6" | "IN_7";
type BusOut = "OUT_0" | "OUT_1" | "OUT_2" | "OUT_3" | "OUT_4" | "OUT_5" | "OUT_6" | "OUT_7";
type BusInB = "IN_B_0" | "IN_B_1" | "IN_B_2" | "IN_B_3" | "IN_B_4" | "IN_B_5" | "IN_B_6" | "IN_B_7";
type BusOutInv = "OUTINV_0" | "OUTINV_1" | "OUTINV_2" | "OUTINV_3" | "OUTINV_4" | "OUTINV_5" | "OUTINV_6" | "OUTINV_7";
type BusSel = "SEL_0" | "SEL_1" | "SEL_2" | "SEL_3";
type BusAddress = "ADDRESS_0" | "ADDRESS_1" | "ADDRESS_2" | "ADDRESS_3" | "ADDRESS_4" | "ADDRESS_5" | "ADDRESS_6" | "ADDRESS_7";
type BusDataIn = "DATA_IN_0" | "DATA_IN_1" | "DATA_IN_2" | "DATA_IN_3" | "DATA_IN_4" | "DATA_IN_5" | "DATA_IN_6" | "DATA_IN_7";
type BusDataOut = "DATA_OUT_0" | "DATA_OUT_1" | "DATA_OUT_2" | "DATA_OUT_3" | "DATA_OUT_4" | "DATA_OUT_5" | "DATA_OUT_6" | "DATA_OUT_7";

// ─── Per-gate input pin maps ────────────────────────────────────────

/** Maps each gate type to its valid input pin names. */
export interface GateInputPins {
  AND: BusIn | EnablePins;
  OR: BusIn | EnablePins;
  XOR: BusIn | EnablePins;
  EQUIVALENCE: BusIn | EnablePins;
  BUFFER: BusIn | EnablePins;
  CLOCK: EnablePins;
  PULSE: EnablePins;
  DRIVER: EnablePins;
  MUX: BusIn | BusSel | EnablePins;
  DECODER: BusIn | "ENABLE" | "ENABLE_B" | "ENABLE_C" | EnablePins;
  PRI_ENCODER: BusIn | "ENABLE" | EnablePins;
  ADDER: BusIn | BusInB | "carry_in" | EnablePins;
  COMPARE: BusIn | BusInB | "in_A_equal_B" | "in_A_greater_B" | "in_A_less_B" | EnablePins;
  JKFF: "clock" | "J" | "K" | "set" | "clear" | EnablePins;
  REGISTER: BusIn | "clock" | "clock_enable" | "clear" | "set" | "load" | "count_enable" | "count_up" | "shift_enable" | "shift_left" | "carry_in" | EnablePins;
  RAM: BusAddress | BusDataIn | "write_clock" | "write_enable" | EnablePins;
  FROM: EnablePins;
  TO: EnablePins;
  TGATE: "T_in" | "T_in2" | "T_ctrl" | EnablePins;
  NODE: "N_in0" | "N_in1" | "N_in2" | "N_in3" | "N_in4" | "N_in5" | "N_in6" | "N_in7" | EnablePins;
  BUS_END: BusIn | BusOut | EnablePins;
  Pauseulator: "signal" | EnablePins;
}

/** Maps each gate type to its valid output pin names. */
export interface GateOutputPins {
  AND: "OUT";
  OR: "OUT";
  XOR: "OUT";
  EQUIVALENCE: "OUT";
  BUFFER: BusOut;
  CLOCK: "CLK";
  PULSE: "OUT_0";
  DRIVER: BusOut;
  MUX: "OUT";
  DECODER: BusOut;
  PRI_ENCODER: BusOut | "VALID";
  ADDER: BusOut | "carry_out" | "overflow";
  COMPARE: "A_equal_B" | "A_greater_B" | "A_less_B";
  JKFF: "Q" | "nQ";
  REGISTER: BusOut | BusOutInv | "carry_out";
  RAM: BusDataOut;
  FROM: never;
  TO: never;
  TGATE: never;
  NODE: never;
  BUS_END: never;
  Pauseulator: never;
}

/** Maps each gate type to its valid parameter names. */
export interface GateParams {
  AND: "INPUT_BITS" | "DEFAULT_DELAY";
  OR: "INPUT_BITS" | "DEFAULT_DELAY";
  XOR: "INPUT_BITS" | "DEFAULT_DELAY";
  EQUIVALENCE: "INPUT_BITS" | "DEFAULT_DELAY";
  BUFFER: "INPUT_BITS" | "DEFAULT_DELAY";
  CLOCK: "HALF_CYCLE" | "DEFAULT_DELAY";
  PULSE: "PULSE" | "DEFAULT_DELAY";
  DRIVER: "OUTPUT_BITS" | "OUTPUT_NUM" | "DEFAULT_DELAY";
  MUX: "INPUT_BITS" | "DEFAULT_DELAY";
  DECODER: "INPUT_BITS" | "DEFAULT_DELAY";
  PRI_ENCODER: "INPUT_BITS" | "DEFAULT_DELAY";
  ADDER: "INPUT_BITS" | "DEFAULT_DELAY";
  COMPARE: "INPUT_BITS" | "DEFAULT_DELAY";
  JKFF: "SYNC_SET" | "SYNC_CLEAR" | "DEFAULT_DELAY";
  REGISTER: "INPUT_BITS" | "CURRENT_VALUE" | "UNKNOWN_OUTPUTS" | "MAX_COUNT" | "SYNC_SET" | "SYNC_CLEAR" | "SYNC_LOAD" | "NO_HOLD" | "DEFAULT_DELAY";
  RAM: "ADDRESS_BITS" | "DATA_BITS" | "READ_FILE" | "WRITE_FILE" | "MemoryReset" | "DEFAULT_DELAY";
  FROM: "JUNCTION_ID" | "DEFAULT_DELAY";
  TO: "JUNCTION_ID" | "DEFAULT_DELAY";
  TGATE: "DEFAULT_DELAY";
  NODE: "DEFAULT_DELAY";
  BUS_END: "INPUT_BITS" | "DEFAULT_DELAY";
  Pauseulator: "PAUSE_SIM" | "DEFAULT_DELAY";
}

// ─── Branded gate ID ────────────────────────────────────────────────

/** A gate ID branded with its gate type. Just a number at runtime. */
export type GateId<T extends GateType = GateType> = number & { readonly __gateType: T };

// ─── Core interfaces ────────────────────────────────────────────────

export interface WireChange {
  id: number;
  state: WireState;
}

export interface StepResult {
  changedWires: WireChange[];
  time: number;
}

export interface Circuit {
  /** Create a gate with a specific ID. Returns a branded gate ID. */
  newGate<T extends GateType>(type: T, gateID: number): GateId<T>;
  /** Create a gate with an auto-assigned ID. Returns a branded gate ID. */
  newGateAuto<T extends GateType>(type: T): GateId<T>;
  /** Create a wire with a specific ID. */
  newWire(wireID: number): number;
  /** Create a wire with an auto-assigned ID. */
  newWireAuto(): number;

  deleteGate(gateID: GateId): void;
  deleteWire(wireID: number): void;

  /** Connect a wire to a gate's named input pin. */
  connectGateInput<T extends GateType>(gateID: GateId<T>, inputID: GateInputPins[T], wireID: number): number;
  /** Connect a gate's named output pin to a wire. */
  connectGateOutput<T extends GateType>(gateID: GateId<T>, outputID: GateOutputPins[T], wireID: number): number;
  disconnectGateInput<T extends GateType>(gateID: GateId<T>, inputID: GateInputPins[T]): void;
  disconnectGateOutput<T extends GateType>(gateID: GateId<T>, outputID: GateOutputPins[T]): void;

  setGateParameter<T extends GateType>(gateID: GateId<T>, paramName: GateParams[T], value: string): void;
  getGateParameter<T extends GateType>(gateID: GateId<T>, paramName: GateParams[T]): string;
  setGateInputParameter<T extends GateType>(gateID: GateId<T>, inputID: GateInputPins[T], paramName: string, value: string): void;
  setGateOutputParameter<T extends GateType>(gateID: GateId<T>, outputID: GateOutputPins[T], paramName: string, value: string): void;

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
