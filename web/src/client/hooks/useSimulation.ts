import { useEffect, useRef } from "react";
import * as Y from "yjs";
import type { WorkerToMainMessage } from "../lib/worker/protocol";
import { createYjsToWorkerBridge } from "../lib/collab/yjs-to-worker";
import { useSimulationStore } from "../stores/simulation-store";

export function useSimulation(doc: Y.Doc | null) {
  const workerRef = useRef<Worker | null>(null);
  const bridgeRef = useRef<ReturnType<typeof createYjsToWorkerBridge> | null>(null);

  const { running, stepsPerFrame, updateWireStates, setSimTime, setRunning } =
    useSimulationStore();

  useEffect(() => {
    if (!doc) return;

    const worker = new Worker(
      new URL("../lib/worker/simulation.worker.ts", import.meta.url),
      { type: "module" }
    );
    workerRef.current = worker;

    worker.onmessage = (e: MessageEvent<WorkerToMainMessage>) => {
      const msg = e.data;
      switch (msg.type) {
        case "ready":
          // Do full sync once worker is ready
          bridgeRef.current?.fullSync();
          break;
        case "wireStates":
          updateWireStates(msg.states.map((s) => ({ id: s.id, state: s.state as any })));
          break;
        case "time":
          setSimTime(msg.time);
          break;
        case "error":
          console.error("[Simulation Worker]", msg.message);
          break;
      }
    };

    const bridge = createYjsToWorkerBridge(doc, (msg) => worker.postMessage(msg));
    bridgeRef.current = bridge;

    worker.postMessage({ type: "init" });

    return () => {
      worker.terminate();
      workerRef.current = null;
      bridgeRef.current = null;
    };
  }, [doc, updateWireStates, setSimTime]);

  // Sync running state to worker
  useEffect(() => {
    workerRef.current?.postMessage({
      type: "setRunning",
      running,
      stepsPerFrame,
    });
  }, [running, stepsPerFrame]);
}
