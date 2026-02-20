import { useEffect, useRef } from "react";
import * as Y from "yjs";
import type { WorkerToMainMessage } from "../lib/worker/protocol";
import { createYjsToWorkerBridge } from "../lib/collab/yjs-to-worker";
import { useSimulationStore, updateWireStates } from "../stores/simulation-store";

export function useSimulation(doc: Y.Doc | null) {
  const workerRef = useRef<Worker | null>(null);
  const bridgeRef = useRef<ReturnType<typeof createYjsToWorkerBridge> | null>(null);
  const genRef = useRef(0);

  const running = useSimulationStore((s) => s.running);
  const stepsPerFrame = useSimulationStore((s) => s.stepsPerFrame);

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
          bridgeRef.current?.fullSync();
          const { running: r, stepsPerFrame: s } = useSimulationStore.getState();
          worker.postMessage({ type: "setRunning", running: r, stepsPerFrame: s, gen: genRef.current });
          break;
        case "wireStates":
          if (msg.gen !== genRef.current) break;
          updateWireStates(msg.states.map((s) => ({ id: s.id, state: s.state as any })));
          break;
        case "time":
          if (msg.gen !== genRef.current) break;
          useSimulationStore.getState().setSimTime(msg.time);
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
  }, [doc]);

  useEffect(() => {
    if (!workerRef.current) return;
    genRef.current++;
    workerRef.current.postMessage({
      type: "setRunning",
      running,
      stepsPerFrame,
      gen: genRef.current,
    });
  }, [running, stepsPerFrame]);
}
