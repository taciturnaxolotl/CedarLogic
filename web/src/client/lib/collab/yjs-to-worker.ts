import * as Y from "yjs";
import { getGatesMap, getWiresMap, getConnectionsMap } from "./yjs-schema";
import type {
  MainToWorkerMessage,
  FullSyncGate,
  FullSyncWire,
  FullSyncConnection,
} from "../worker/protocol";
import type { GateDefinition } from "@shared/types";
import gateDefsRaw from "../canvas/gate-defs.json";

const gateDefMap = new Map<string, GateDefinition>();
for (const def of gateDefsRaw as unknown as GateDefinition[]) {
  gateDefMap.set(def.id, def);
}

function getInvertedPins(defId: string): { invertedInputs?: string[]; invertedOutputs?: string[] } {
  const def = gateDefMap.get(defId);
  if (!def) return {};
  const invertedInputs = def.inputs.filter(p => p.inverted).map(p => p.name);
  const invertedOutputs = def.outputs.filter(p => p.inverted).map(p => p.name);
  const result: { invertedInputs?: string[]; invertedOutputs?: string[] } = {};
  if (invertedInputs.length > 0) result.invertedInputs = invertedInputs;
  if (invertedOutputs.length > 0) result.invertedOutputs = invertedOutputs;
  return result;
}

/**
 * Observes Yjs maps and sends incremental updates to the simulation worker.
 * Also provides fullSync() for initial load.
 */
export function createYjsToWorkerBridge(
  doc: Y.Doc,
  postToWorker: (msg: MainToWorkerMessage) => void
) {
  const gates = getGatesMap(doc);
  const wires = getWiresMap(doc);
  const connections = getConnectionsMap(doc);

  // O(1) reverse lookup: Y.Map instance → gate ID
  const yMapToGateId = new WeakMap<Y.Map<any>, string>();

  function populateGateIdMap() {
    gates.forEach((yGate, id) => {
      yMapToGateId.set(yGate, id);
    });
  }

  function fullSync() {
    populateGateIdMap();

    const syncGates: FullSyncGate[] = [];
    gates.forEach((yGate, id) => {
      const params: Record<string, string> = {};
      for (const [key, val] of yGate.entries()) {
        if (key.startsWith("param:")) {
          params[key.replace("param:", "")] = val;
        }
      }
      const defId = yGate.get("defId") as string;
      syncGates.push({ id, logicType: yGate.get("logicType"), params, ...getInvertedPins(defId) });
    });

    const syncWires: FullSyncWire[] = [];
    wires.forEach((_, id) => {
      syncWires.push({ id });
    });

    const syncConnections: FullSyncConnection[] = [];
    connections.forEach((yConn) => {
      syncConnections.push({
        gateId: yConn.get("gateId"),
        pinName: yConn.get("pinName"),
        pinDirection: yConn.get("pinDirection"),
        wireId: yConn.get("wireId"),
      });
    });

    postToWorker({
      type: "fullSync",
      gates: syncGates,
      wires: syncWires,
      connections: syncConnections,
    });
  }

  // Observe gates
  gates.observeDeep((events) => {
    for (const event of events) {
      if (event.target === gates && event instanceof Y.YMapEvent) {
        // Top-level gate add/delete
        for (const [key, change] of event.changes.keys) {
          if (change.action === "add") {
            const yGate = gates.get(key);
            if (!yGate) continue;
            // Register in reverse-lookup map
            yMapToGateId.set(yGate, key);
            const params: Record<string, string> = {};
            for (const [k, v] of yGate.entries()) {
              if (k.startsWith("param:")) params[k.replace("param:", "")] = v;
            }
            const defId = yGate.get("defId") as string;
            postToWorker({
              type: "addGate",
              id: key,
              logicType: yGate.get("logicType"),
              params,
              ...getInvertedPins(defId),
            });
          } else if (change.action === "delete") {
            postToWorker({ type: "removeGate", id: key });
          }
        }
      } else if (
        event.target !== gates &&
        event.target instanceof Y.Map &&
        event instanceof Y.YMapEvent
      ) {
        // Inner gate parameter change — O(1) lookup instead of O(N) scan
        const yGate = event.target as Y.Map<any>;
        const gateId = yMapToGateId.get(yGate) ?? null;
        if (!gateId) continue;

        for (const [key, change] of event.changes.keys) {
          if (key.startsWith("param:") && (change.action === "add" || change.action === "update")) {
            const paramName = key.replace("param:", "");
            const value = yGate.get(key);
            postToWorker({
              type: "setParam",
              gateId,
              paramName,
              value,
            });
          }
        }
      }
    }
  });

  // Observe wires
  wires.observeDeep((events) => {
    for (const event of events) {
      if (event.target === wires && event instanceof Y.YMapEvent) {
        for (const [key, change] of event.changes.keys) {
          if (change.action === "add") {
            postToWorker({ type: "addWire", id: key });
          } else if (change.action === "delete") {
            postToWorker({ type: "removeWire", id: key });
          }
        }
      }
    }
  });

  // Observe connections — any add or delete triggers a full sync because
  // the worker merges wires that share a gate pin into a single WASM wire,
  // which requires the full connection picture to compute correctly.
  connections.observeDeep((events) => {
    for (const event of events) {
      if (event.target === connections && event instanceof Y.YMapEvent) {
        if (event.changes.keys.size > 0) {
          fullSync();
          return;
        }
      }
    }
  });

  return { fullSync };
}
