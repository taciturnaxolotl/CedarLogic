import { useRef, useEffect } from "react";
import { Layer } from "react-konva";
import Konva from "konva";
import type { Awareness } from "y-protocols/awareness";

interface CursorLayerProps {
  awareness: Awareness | null;
}

/**
 * Renders remote cursors by subscribing to awareness directly and
 * updating Konva nodes imperatively — bypasses React reconciliation
 * for cursor position updates.
 */
export function CursorLayer({ awareness }: CursorLayerProps) {
  const layerRef = useRef<Konva.Layer>(null);
  const groupsRef = useRef<Map<number, Konva.Group>>(new Map());

  useEffect(() => {
    if (!awareness) return;
    const layer = layerRef.current;
    if (!layer) return;

    function sync() {
      const existing = groupsRef.current;
      const activeIds = new Set<number>();

      awareness!.getStates().forEach((state, clientId) => {
        if (clientId === awareness!.clientID) return;
        if (!state.cursor) return;
        activeIds.add(clientId);

        const { x, y, user } = state.cursor;
        let group = existing.get(clientId);

        if (!group) {
          group = new Konva.Group({ x, y });

          const arrow = new Konva.Line({
            points: [0, 0, 0, 14, 4, 11, 8, 18, 11, 16, 7, 10, 12, 10],
            fill: user.color,
            closed: true,
            stroke: "#000",
            strokeWidth: 0.5,
          });

          const labelGroup = new Konva.Group({ x: 12, y: 14 });
          const labelBg = new Konva.Rect({
            width: user.name.length * 7 + 8,
            height: 18,
            fill: user.color,
            cornerRadius: 3,
          });
          const labelText = new Konva.Text({
            text: user.name,
            fontSize: 11,
            fill: "#fff",
            x: 4,
            y: 3,
            fontFamily: "system-ui, sans-serif",
          });

          labelGroup.add(labelBg, labelText);
          group.add(arrow, labelGroup);
          layer!.add(group);
          existing.set(clientId, group);
        } else {
          group.position({ x, y });
        }
      });

      // Remove cursors for clients that left
      for (const [clientId, group] of existing) {
        if (!activeIds.has(clientId)) {
          group.destroy();
          existing.delete(clientId);
        }
      }

      layer!.batchDraw();
    }

    awareness.on("change", sync);
    sync();

    return () => {
      awareness.off("change", sync);
      for (const group of groupsRef.current.values()) {
        group.destroy();
      }
      groupsRef.current.clear();
    };
  }, [awareness]);

  return <Layer ref={layerRef} listening={false} />;
}
