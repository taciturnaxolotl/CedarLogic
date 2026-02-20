import { useRef, useEffect, useState } from "react";
import type { ThumbnailData, GateDefinition } from "@shared/types";
import { loadGateDefs } from "./canvas/GateLayer";
import { GRID_SIZE } from "@shared/constants";

interface CircuitThumbnailProps {
  data: ThumbnailData | undefined;
}

export function CircuitThumbnail({ data }: CircuitThumbnailProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [defs, setDefs] = useState<GateDefinition[]>([]);

  useEffect(() => {
    loadGateDefs().then(setDefs);
  }, []);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || defs.length === 0) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);

    const w = rect.width;
    const h = rect.height;

    ctx.clearRect(0, 0, w, h);

    if (!data) return;
    if (data.gates.length === 0 && data.wires.length === 0) return;

    const defsMap = new Map<string, GateDefinition>(
      defs.map((d) => [d.id, d])
    );

    // Compute bounding box
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;

    for (const gate of data.gates) {
      const def = defsMap.get(gate.defId);
      if (def) {
        for (const seg of def.shape) {
          const points = [
            { x: gate.x + seg.x1 * GRID_SIZE, y: gate.y + seg.y1 * GRID_SIZE },
            { x: gate.x + seg.x2 * GRID_SIZE, y: gate.y + seg.y2 * GRID_SIZE },
          ];
          for (const p of points) {
            minX = Math.min(minX, p.x);
            minY = Math.min(minY, p.y);
            maxX = Math.max(maxX, p.x);
            maxY = Math.max(maxY, p.y);
          }
        }
      } else {
        minX = Math.min(minX, gate.x - GRID_SIZE);
        minY = Math.min(minY, gate.y - GRID_SIZE);
        maxX = Math.max(maxX, gate.x + GRID_SIZE);
        maxY = Math.max(maxY, gate.y + GRID_SIZE);
      }
    }

    for (const wire of data.wires) {
      for (const seg of wire.segments) {
        minX = Math.min(minX, seg.x1, seg.x2);
        minY = Math.min(minY, seg.y1, seg.y2);
        maxX = Math.max(maxX, seg.x1, seg.x2);
        maxY = Math.max(maxY, seg.y1, seg.y2);
      }
    }

    if (!isFinite(minX)) return;

    const padding = 16;
    const bboxW = maxX - minX || 1;
    const bboxH = maxY - minY || 1;
    const scale = Math.min(
      (w - padding * 2) / bboxW,
      (h - padding * 2) / bboxH
    );
    const offsetX = (w - bboxW * scale) / 2 - minX * scale;
    const offsetY = (h - bboxH * scale) / 2 - minY * scale;

    ctx.save();
    ctx.translate(offsetX, offsetY);
    ctx.scale(scale, scale);

    // Draw wires
    ctx.strokeStyle = "#555";
    ctx.lineWidth = 2 / scale;
    ctx.lineCap = "round";
    for (const wire of data.wires) {
      for (const seg of wire.segments) {
        ctx.beginPath();
        ctx.moveTo(seg.x1, seg.y1);
        ctx.lineTo(seg.x2, seg.y2);
        ctx.stroke();
      }
    }

    // Draw gates
    ctx.strokeStyle = "#888";
    ctx.lineWidth = 1.5 / scale;
    for (const gate of data.gates) {
      const def = defsMap.get(gate.defId);
      if (!def) continue;

      ctx.save();
      ctx.translate(gate.x, gate.y);
      if (gate.rotation) {
        ctx.rotate((gate.rotation * Math.PI) / 180);
      }

      for (const seg of def.shape) {
        ctx.beginPath();
        ctx.moveTo(seg.x1 * GRID_SIZE, seg.y1 * GRID_SIZE);
        ctx.lineTo(seg.x2 * GRID_SIZE, seg.y2 * GRID_SIZE);
        ctx.stroke();
      }

      for (const c of def.circles ?? []) {
        ctx.beginPath();
        ctx.arc(c.cx * GRID_SIZE, c.cy * GRID_SIZE, c.r * GRID_SIZE, 0, Math.PI * 2);
        ctx.stroke();
      }

      ctx.restore();
    }

    ctx.restore();
  }, [data, defs]);

  if (data === undefined) {
    return (
      <div className="w-full aspect-[4/3] bg-gray-800 rounded-t-xl animate-pulse" />
    );
  }

  return (
    <canvas
      ref={canvasRef}
      className="w-full aspect-[4/3] rounded-t-xl bg-gray-800/50"
      style={{ display: "block" }}
    />
  );
}
