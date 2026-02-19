import { useRef, useCallback, useEffect, useState } from "react";
import { Stage, Layer } from "react-konva";
import type Konva from "konva";
import * as Y from "yjs";
import { useCanvasStore } from "../stores/canvas-store";
import { GridLayer } from "./canvas/GridLayer";
import { GateLayer } from "./canvas/GateLayer";
import { WireLayer } from "./canvas/WireLayer";
import { OverlayLayer } from "./canvas/OverlayLayer";
import { SNAP_SIZE } from "@shared/constants";
import {
  getGatesMap,
  getWiresMap,
  getConnectionsMap,
  addGateToDoc,
  addWireToDoc,
  addConnectionToDoc,
} from "../lib/collab/yjs-schema";

interface CanvasProps {
  doc: Y.Doc;
  readOnly: boolean;
}

function snapToGrid(val: number): number {
  return Math.round(val / SNAP_SIZE) * SNAP_SIZE;
}

type DragMode = "none" | "pan" | "select-box";

export function Canvas({ doc, readOnly }: CanvasProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const stageRef = useRef<Konva.Stage>(null);
  const [size, setSize] = useState({ width: 0, height: 0 });

  const dragMode = useRef<DragMode>("none");
  const dragStart = useRef({ x: 0, y: 0 });
  const viewportStart = useRef({ x: 0, y: 0 });

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const ro = new ResizeObserver(([entry]) => {
      setSize({
        width: entry.contentRect.width,
        height: entry.contentRect.height,
      });
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const viewportX = useCanvasStore((s) => s.viewportX);
  const viewportY = useCanvasStore((s) => s.viewportY);
  const zoom = useCanvasStore((s) => s.zoom);
  const setViewport = useCanvasStore((s) => s.setViewport);
  const clearSelection = useCanvasStore((s) => s.clearSelection);
  const selectedIds = useCanvasStore((s) => s.selectedIds);
  const setSelectionBox = useCanvasStore((s) => s.setSelectionBox);
  const wireDrawing = useCanvasStore((s) => s.wireDrawing);
  const setWireDrawing = useCanvasStore((s) => s.setWireDrawing);

  const undoManager = useRef<Y.UndoManager | null>(null);

  useEffect(() => {
    const gates = getGatesMap(doc);
    const wires = getWiresMap(doc);
    const connections = getConnectionsMap(doc);
    undoManager.current = new Y.UndoManager([gates, wires, connections]);
    return () => {
      undoManager.current?.destroy();
    };
  }, [doc]);

  // Keyboard shortcuts
  useEffect(() => {
    function handleKeyDown(e: KeyboardEvent) {
      if (e.key === "Delete" || e.key === "Backspace") {
        const ids = Object.keys(selectedIds);
        if (readOnly || ids.length === 0) return;
        doc.transact(() => {
          const gates = getGatesMap(doc);
          const wires = getWiresMap(doc);
          const connections = getConnectionsMap(doc);

          // Collect wire IDs that should be removed when their gate is deleted
          const wireIdsToRemove = new Set<string>();

          for (const id of ids) {
            // If deleting a gate, find all connected wires
            if (gates.has(id)) {
              for (const [, conn] of connections.entries()) {
                if (conn.get("gateId") === id) {
                  wireIdsToRemove.add(conn.get("wireId"));
                }
              }
            }

            gates.delete(id);
            wires.delete(id);
          }

          // Remove wires connected to deleted gates
          for (const wireId of wireIdsToRemove) {
            wires.delete(wireId);
          }

          // Remove all connections referencing deleted gates or wires
          for (const [key, conn] of connections.entries()) {
            const gateId = conn.get("gateId");
            const wireId = conn.get("wireId");
            if (ids.includes(gateId) || ids.includes(wireId) || wireIdsToRemove.has(wireId)) {
              connections.delete(key);
            }
          }
        });
        clearSelection();
      }
      if (e.key === "z" && (e.ctrlKey || e.metaKey) && !e.shiftKey) {
        e.preventDefault();
        undoManager.current?.undo();
      }
      if (
        (e.key === "y" && (e.ctrlKey || e.metaKey)) ||
        (e.key === "z" && (e.ctrlKey || e.metaKey) && e.shiftKey)
      ) {
        e.preventDefault();
        undoManager.current?.redo();
      }
      if (e.key === "Escape") {
        setWireDrawing(null);
        clearSelection();
      }
    }
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [doc, readOnly, selectedIds, clearSelection, setWireDrawing]);

  const handleWheel = useCallback(
    (e: Konva.KonvaEventObject<WheelEvent>) => {
      e.evt.preventDefault();
      const stage = stageRef.current;
      if (!stage) return;

      if (e.evt.ctrlKey || e.evt.metaKey) {
        const oldZoom = zoom;
        const pointer = stage.getPointerPosition()!;
        const mousePointTo = {
          x: (pointer.x - viewportX) / oldZoom,
          y: (pointer.y - viewportY) / oldZoom,
        };
        const newZoom = Math.max(
          0.1,
          Math.min(5, oldZoom * (e.evt.deltaY < 0 ? 1.1 : 0.9))
        );
        setViewport(
          pointer.x - mousePointTo.x * newZoom,
          pointer.y - mousePointTo.y * newZoom,
          newZoom
        );
      } else {
        setViewport(
          viewportX - e.evt.deltaX,
          viewportY - e.evt.deltaY,
          zoom
        );
      }
    },
    [viewportX, viewportY, zoom, setViewport]
  );

  const handleMouseDown = useCallback(
    (e: Konva.KonvaEventObject<MouseEvent>) => {
      const stage = stageRef.current;
      if (!stage) return;

      // If we're wire-drawing and click on empty space, cancel it
      const wd = useCanvasStore.getState().wireDrawing;
      if (wd && e.target === stage) {
        setWireDrawing(null);
        return;
      }

      if (e.target !== stage) return;

      if (e.evt.button === 1 || e.evt.button === 2) {
        dragMode.current = "pan";
        dragStart.current = { x: e.evt.clientX, y: e.evt.clientY };
        viewportStart.current = { x: viewportX, y: viewportY };
      } else if (e.evt.button === 0) {
        clearSelection();
        dragMode.current = "select-box";
        const pointer = stage.getPointerPosition()!;
        dragStart.current = {
          x: (pointer.x - viewportX) / zoom,
          y: (pointer.y - viewportY) / zoom,
        };
      }
    },
    [viewportX, viewportY, zoom, clearSelection, setWireDrawing]
  );

  const handleMouseMove = useCallback(
    (e: Konva.KonvaEventObject<MouseEvent>) => {
      const stage = stageRef.current;
      if (!stage) return;

      // Update wire drawing preview
      const wd = useCanvasStore.getState().wireDrawing;
      if (wd) {
        const pointer = stage.getPointerPosition()!;
        const x = snapToGrid((pointer.x - viewportX) / zoom);
        const y = snapToGrid((pointer.y - viewportY) / zoom);
        setWireDrawing({ ...wd, currentX: x, currentY: y });
        return;
      }

      if (dragMode.current === "pan") {
        const dx = e.evt.clientX - dragStart.current.x;
        const dy = e.evt.clientY - dragStart.current.y;
        setViewport(
          viewportStart.current.x + dx,
          viewportStart.current.y + dy,
          zoom
        );
      } else if (dragMode.current === "select-box") {
        const pointer = stage.getPointerPosition()!;
        const currentX = (pointer.x - viewportX) / zoom;
        const currentY = (pointer.y - viewportY) / zoom;
        setSelectionBox({
          x: Math.min(dragStart.current.x, currentX),
          y: Math.min(dragStart.current.y, currentY),
          width: Math.abs(currentX - dragStart.current.x),
          height: Math.abs(currentY - dragStart.current.y),
        });
      }
    },
    [viewportX, viewportY, zoom, setViewport, setSelectionBox, setWireDrawing]
  );

  const handleMouseUp = useCallback(() => {
    if (dragMode.current === "select-box") {
      setSelectionBox(null);
    }
    dragMode.current = "none";
  }, [setSelectionBox]);

  const handleContextMenu = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
  }, []);

  // Drag-and-drop gates from toolbar
  const handleDrop = useCallback(
    (e: React.DragEvent) => {
      e.preventDefault();
      if (readOnly) return;

      const data = e.dataTransfer.getData("application/cedarlogic-gate");
      if (!data) return;

      const { defId, logicType, params } = JSON.parse(data);
      const container = containerRef.current;
      if (!container) return;

      const rect = container.getBoundingClientRect();
      const x = snapToGrid((e.clientX - rect.left - viewportX) / zoom);
      const y = snapToGrid((e.clientY - rect.top - viewportY) / zoom);

      const id = crypto.randomUUID();
      addGateToDoc(doc, id, {
        defId,
        logicType: logicType || "",
        x,
        y,
        rotation: 0,
        ...Object.fromEntries(
          Object.entries(params || {}).map(([k, v]) => [`param:${k}`, v])
        ),
      });
    },
    [readOnly, doc, viewportX, viewportY, zoom]
  );

  const handleDragOver = useCallback((e: React.DragEvent) => {
    if (e.dataTransfer.types.includes("application/cedarlogic-gate")) {
      e.preventDefault();
      e.dataTransfer.dropEffect = "copy";
    }
  }, []);

  return (
    <div
      ref={containerRef}
      className="w-full h-full"
      onDrop={handleDrop}
      onDragOver={handleDragOver}
      onContextMenu={handleContextMenu}
    >
      {size.width > 0 && size.height > 0 && (
        <Stage
          ref={stageRef}
          width={size.width}
          height={size.height}
          x={viewportX}
          y={viewportY}
          scaleX={zoom}
          scaleY={zoom}
          onWheel={handleWheel}
          onMouseDown={handleMouseDown}
          onMouseMove={handleMouseMove}
          onMouseUp={handleMouseUp}
        >
          <GridLayer />
          <WireLayer doc={doc} readOnly={readOnly} />
          <GateLayer doc={doc} readOnly={readOnly} />
          <OverlayLayer />
        </Stage>
      )}
    </div>
  );
}
