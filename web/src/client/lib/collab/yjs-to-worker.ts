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

  let debounceTimer: ReturnType<typeof setTimeout> | null = null;

  function debouncedStep() {
    if (debounceTimer) clearTimeout(debounceTimer);
    debounceTimer = setTimeout(() => {
      postToWorker({ type: "step", count: 5 });
    }, 100);
  }

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
            debouncedStep();
          } else if (change.action === "delete") {
            postToWorker({ type: "removeGate", id: key });
            debouncedStep();
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
            debouncedStep();
          }
        }
      }
    }
  });

  // Observe connections
  connections.observeDeep((events) => {
    for (const event of events) {
      if (event.target === connections && event instanceof Y.YMapEvent) {
        for (const [key, change] of event.changes.keys) {
          if (change.action === "add") {
            const yConn = connections.get(key);
            if (!yConn) continue;
            postToWorker({
              type: "connect",
              gateId: yConn.get("gateId"),
              pinName: yConn.get("pinName"),
              pinDirection: yConn.get("pinDirection"),
              wireId: yConn.get("wireId"),
            });
            debouncedStep();
          } else if (change.action === "delete") {
            // Connection removed — need to disconnect
            // We don't have the old value, so full sync is safest
            fullSync();
          }
        }
      }
    }
  });

  return { fullSync };
}
