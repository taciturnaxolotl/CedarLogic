import { useRef, useCallback, useEffect, useState } from "react";
import { Stage, Layer } from "react-konva";
import type Konva from "konva";
import * as Y from "yjs";
import { useCanvasStore, type ClipboardData } from "../stores/canvas-store";
import { GridLayer } from "./canvas/GridLayer";
import { GateLayer, loadedGateDefs, getGateBounds } from "./canvas/GateLayer";
import { WireLayer } from "./canvas/WireLayer";
import { OverlayLayer } from "./canvas/OverlayLayer";
import { SNAP_SIZE } from "@shared/constants";
import type { GateDefinition, WireSegment } from "@shared/types";
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

/** Check if two axis-aligned rectangles intersect */
function rectsIntersect(
  a: { x: number; y: number; width: number; height: number },
  b: { x: number; y: number; width: number; height: number },
): boolean {
  return (
    a.x < b.x + b.width &&
    a.x + a.width > b.x &&
    a.y < b.y + b.height &&
    a.y + a.height > b.y
  );
}

/** Check if a line segment intersects a rectangle */
function segmentIntersectsRect(
  seg: WireSegment,
  rect: { x: number; y: number; width: number; height: number },
): boolean {
  const rx = rect.x;
  const ry = rect.y;
  const rr = rect.x + rect.width;
  const rb = rect.y + rect.height;

  // Check if either endpoint is inside the rect
  if (seg.x1 >= rx && seg.x1 <= rr && seg.y1 >= ry && seg.y1 <= rb) return true;
  if (seg.x2 >= rx && seg.x2 <= rr && seg.y2 >= ry && seg.y2 <= rb) return true;

  // Check if the segment (treated as an axis-aligned line) crosses the rect
  const minX = Math.min(seg.x1, seg.x2);
  const maxX = Math.max(seg.x1, seg.x2);
  const minY = Math.min(seg.y1, seg.y2);
  const maxY = Math.max(seg.y1, seg.y2);

  // Horizontal segment
  if (seg.y1 === seg.y2) {
    return seg.y1 >= ry && seg.y1 <= rb && minX <= rr && maxX >= rx;
  }
  // Vertical segment
  if (seg.x1 === seg.x2) {
    return seg.x1 >= rx && seg.x1 <= rr && minY <= rb && maxY >= ry;
  }

  // General segment — bounding box overlap as fallback
  return minX <= rr && maxX >= rx && minY <= rb && maxY >= ry;
}

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
  const select = useCanvasStore((s) => s.select);
  const selectedIds = useCanvasStore((s) => s.selectedIds);
  const setSelectionBox = useCanvasStore((s) => s.setSelectionBox);
  const wireDrawing = useCanvasStore((s) => s.wireDrawing);
  const setWireDrawing = useCanvasStore((s) => s.setWireDrawing);
  const setClipboard = useCanvasStore((s) => s.setClipboard);
  const setPendingPaste = useCanvasStore((s) => s.setPendingPaste);
  const mousePos = useRef({ x: 0, y: 0 });

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

  /** Commit a pending paste at the given position */
  const commitPaste = useCallback(
    (data: ClipboardData, pasteX: number, pasteY: number) => {
      const gateIdMap = new Map<string, string>();
      const wireIdMap = new Map<string, string>();

      for (const g of data.gates) {
        gateIdMap.set(g.originalId, crypto.randomUUID());
      }
      for (const w of data.wires) {
        wireIdMap.set(w.originalId, crypto.randomUUID());
      }

      doc.transact(() => {
        for (const g of data.gates) {
          const newId = gateIdMap.get(g.originalId)!;
          addGateToDoc(doc, newId, {
            defId: g.defId,
            logicType: g.logicType,
            x: snapToGrid(pasteX + g.offsetX),
            y: snapToGrid(pasteY + g.offsetY),
            rotation: g.rotation,
            ...g.params,
          });
        }

        for (const w of data.wires) {
          const newId = wireIdMap.get(w.originalId)!;
          const offsetSegments = w.segments.map((s) => ({
            x1: s.x1 + pasteX,
            y1: s.y1 + pasteY,
            x2: s.x2 + pasteX,
            y2: s.y2 + pasteY,
          }));
          addWireToDoc(doc, newId, offsetSegments);
        }

        for (const c of data.connections) {
          const newGateId = gateIdMap.get(c.originalGateId);
          const newWireId = wireIdMap.get(c.originalWireId);
          if (!newGateId || !newWireId) continue;
          addConnectionToDoc(doc, newGateId, c.pinName, c.pinDirection, newWireId);
        }
      });

      clearSelection();
      for (const newId of gateIdMap.values()) {
        select(newId);
      }
      for (const newId of wireIdMap.values()) {
        select(newId);
      }
    },
    [doc, clearSelection, select]
  );

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
      // Copy
      if (e.key === "c" && (e.ctrlKey || e.metaKey)) {
        const ids = Object.keys(selectedIds);
        if (ids.length === 0) return;

        const gatesMap = getGatesMap(doc);
        const wiresMap = getWiresMap(doc);
        const connectionsMap = getConnectionsMap(doc);

        // Collect selected gates
        const selectedGateIds = new Set<string>();
        const gateDataList: Array<{
          id: string;
          defId: string;
          logicType: string;
          x: number;
          y: number;
          rotation: number;
          params: Record<string, string>;
        }> = [];

        for (const id of ids) {
          const yGate = gatesMap.get(id);
          if (!yGate) continue;
          selectedGateIds.add(id);
          const params: Record<string, string> = {};
          for (const [k, v] of yGate.entries()) {
            if (k.startsWith("param:")) params[k] = String(v);
          }
          gateDataList.push({
            id,
            defId: yGate.get("defId"),
            logicType: yGate.get("logicType") ?? "",
            x: yGate.get("x"),
            y: yGate.get("y"),
            rotation: yGate.get("rotation") ?? 0,
            params,
          });
        }

        if (gateDataList.length === 0) return;

        // Compute centroid
        const cx =
          gateDataList.reduce((s, g) => s + g.x, 0) / gateDataList.length;
        const cy =
          gateDataList.reduce((s, g) => s + g.y, 0) / gateDataList.length;

        // Find wires fully internal to selection (both endpoints connected to selected gates)
        const wireEndpoints = new Map<
          string,
          Array<{ gateId: string; pinName: string; pinDirection: "input" | "output" }>
        >();
        connectionsMap.forEach((yConn) => {
          const wireId = yConn.get("wireId") as string;
          const gateId = yConn.get("gateId") as string;
          const pinName = yConn.get("pinName") as string;
          const pinDirection = yConn.get("pinDirection") as "input" | "output";
          const arr = wireEndpoints.get(wireId) || [];
          arr.push({ gateId, pinName, pinDirection });
          wireEndpoints.set(wireId, arr);
        });

        const selectedWireIds = new Set<string>();
        // Also include wires that are directly selected
        for (const id of ids) {
          if (wiresMap.has(id)) {
            selectedWireIds.add(id);
          }
        }
        // Include wires where both endpoints are connected to selected gates
        for (const [wireId, endpoints] of wireEndpoints) {
          if (
            endpoints.length >= 2 &&
            endpoints.every((ep) => selectedGateIds.has(ep.gateId))
          ) {
            selectedWireIds.add(wireId);
          }
        }

        // Build clipboard gate data with offsets from centroid
        const clipGates = gateDataList.map((g) => ({
          defId: g.defId,
          logicType: g.logicType,
          offsetX: g.x - cx,
          offsetY: g.y - cy,
          rotation: g.rotation,
          params: g.params,
          originalId: g.id,
        }));

        // Build clipboard wire data with segments offset from centroid
        const clipWires: Array<{ segments: WireSegment[]; originalId: string }> =
          [];
        for (const wireId of selectedWireIds) {
          const yWire = wiresMap.get(wireId);
          if (!yWire) continue;
          try {
            const segments: WireSegment[] = JSON.parse(
              yWire.get("segments") || "[]"
            );
            // Store segments relative to centroid
            const offsetSegs = segments.map((s) => ({
              x1: s.x1 - cx,
              y1: s.y1 - cy,
              x2: s.x2 - cx,
              y2: s.y2 - cy,
            }));
            clipWires.push({ segments: offsetSegs, originalId: wireId });
          } catch {
            // skip
          }
        }

        // Build clipboard connection data (only for selected wires)
        const clipConnections: Array<{
          originalGateId: string;
          pinName: string;
          pinDirection: "input" | "output";
          originalWireId: string;
        }> = [];
        connectionsMap.forEach((yConn) => {
          const wireId = yConn.get("wireId") as string;
          if (!selectedWireIds.has(wireId)) return;
          const gateId = yConn.get("gateId") as string;
          if (!selectedGateIds.has(gateId)) return;
          clipConnections.push({
            originalGateId: gateId,
            pinName: yConn.get("pinName") as string,
            pinDirection: yConn.get("pinDirection") as "input" | "output",
            originalWireId: wireId,
          });
        });

        setClipboard({
          gates: clipGates,
          wires: clipWires,
          connections: clipConnections,
        });
      }

      // Paste — enter pending paste mode (preview follows mouse, click to place)
      if (e.key === "v" && (e.ctrlKey || e.metaKey)) {
        const cb = useCanvasStore.getState().clipboard;
        if (!cb || readOnly) return;
        e.preventDefault();

        const x = snapToGrid(mousePos.current.x);
        const y = snapToGrid(mousePos.current.y);
        setPendingPaste({ data: cb, x, y });
        clearSelection();
      }

      if (e.key === "Escape") {
        if (useCanvasStore.getState().pendingPaste) {
          setPendingPaste(null);
        } else {
          setWireDrawing(null);
          clearSelection();
        }
      }
    }
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [doc, readOnly, selectedIds, clearSelection, select, setWireDrawing, setClipboard, setPendingPaste]);

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

      // If pending paste, commit it on click
      const pp = useCanvasStore.getState().pendingPaste;
      if (pp && e.evt.button === 0) {
        commitPaste(pp.data, pp.x, pp.y);
        setPendingPaste(null);
        return;
      }

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
    [viewportX, viewportY, zoom, clearSelection, setWireDrawing, setPendingPaste, commitPaste]
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

      // Track mouse position for paste
      const pointer = stage.getPointerPosition();
      if (pointer) {
        mousePos.current = {
          x: (pointer.x - viewportX) / zoom,
          y: (pointer.y - viewportY) / zoom,
        };
      }

      // Update pending paste preview position
      const pp = useCanvasStore.getState().pendingPaste;
      if (pp && pointer) {
        const x = snapToGrid((pointer.x - viewportX) / zoom);
        const y = snapToGrid((pointer.y - viewportY) / zoom);
        setPendingPaste({ ...pp, x, y });
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
    [viewportX, viewportY, zoom, setViewport, setSelectionBox, setWireDrawing, setPendingPaste]
  );

  const handleMouseUp = useCallback(() => {
    if (dragMode.current === "select-box") {
      const box = useCanvasStore.getState().selectionBox;
      if (box && (box.width > 5 || box.height > 5)) {
        // Build defs map from loaded gate defs
        const defsMap = new Map<string, GateDefinition>(
          loadedGateDefs.map((d) => [d.id, d])
        );

        // Hit-test gates
        const gatesMap = getGatesMap(doc);
        gatesMap.forEach((yGate, id) => {
          const def = defsMap.get(yGate.get("defId"));
          if (!def) return;
          const bounds = getGateBounds(def);
          const gateBounds = {
            x: yGate.get("x") + bounds.x,
            y: yGate.get("y") + bounds.y,
            width: bounds.width,
            height: bounds.height,
          };
          if (rectsIntersect(box, gateBounds)) {
            select(id);
          }
        });

        // Hit-test wires
        const wiresMap = getWiresMap(doc);
        wiresMap.forEach((yWire, id) => {
          try {
            const segments: WireSegment[] = JSON.parse(
              yWire.get("segments") || "[]"
            );
            for (const seg of segments) {
              if (segmentIntersectsRect(seg, box)) {
                select(id);
                break;
              }
            }
          } catch {
            // skip malformed wires
          }
        });
      }
      setSelectionBox(null);
    }
    dragMode.current = "none";
  }, [doc, select, setSelectionBox]);

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
