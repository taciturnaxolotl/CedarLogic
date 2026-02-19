import * as Y from "yjs";
import { getGatesMap, getWiresMap, getConnectionsMap } from "./yjs-schema";
import type {
  MainToWorkerMessage,
  FullSyncGate,
  FullSyncWire,
  FullSyncConnection,
} from "../worker/protocol";

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

  function fullSync() {
    const syncGates: FullSyncGate[] = [];
    gates.forEach((yGate, id) => {
      const params: Record<string, string> = {};
      for (const [key, val] of yGate.entries()) {
        if (key.startsWith("param:")) {
          params[key.replace("param:", "")] = val;
        }
      }
      syncGates.push({ id, logicType: yGate.get("logicType"), params });
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
            const params: Record<string, string> = {};
            for (const [k, v] of yGate.entries()) {
              if (k.startsWith("param:")) params[k.replace("param:", "")] = v;
            }
            postToWorker({
              type: "addGate",
              id: key,
              logicType: yGate.get("logicType"),
              params,
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
        // Inner gate parameter change — find the gate ID
        const yGate = event.target as Y.Map<any>;
        let gateId: string | null = null;
        gates.forEach((v, k) => {
          if (v === yGate) gateId = k;
        });
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
