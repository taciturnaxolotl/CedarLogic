import { useRef, useEffect } from "react";
import { Layer } from "react-konva";
import Konva from "konva";
import type { CursorWS } from "../../lib/collab/cursor-ws";
import { createSpring, setSpringTarget, tickSpring, type SpringState } from "../../lib/canvas/spring";
import { useCanvasColors } from "../../hooks/useCanvasColors";

interface CursorState {
  group: Konva.Group;
  spring: SpringState;
}

interface CursorLayerProps {
  cursorWS: CursorWS | null;
  userMeta: Map<number, { name: string; color: string }>;
}

/**
 * Renders remote cursors with spring-interpolated positions.
 * Subscribes to CursorWS binary messages and drives a rAF loop
 * that ticks spring physics and updates Konva nodes imperatively.
 */
export function CursorLayer({ cursorWS, userMeta }: CursorLayerProps) {
  const layerRef = useRef<Konva.Layer>(null);
  const cursorsRef = useRef<Map<number, CursorState>>(new Map());
  const rafRef = useRef<number>(0);
  const lastTimeRef = useRef<number>(0);
  const animatingRef = useRef(false);
  // Keep userMeta in a ref so the effect doesn't depend on it
  const userMetaRef = useRef(userMeta);
  userMetaRef.current = userMeta;
  const colors = useCanvasColors();
  const colorsRef = useRef(colors);
  colorsRef.current = colors;

  useEffect(() => {
    if (!cursorWS) return;
    const layer = layerRef.current;
    if (!layer) return;
    const cursors = cursorsRef.current;

    function ensureCursor(userHash: number): CursorState {
      let c = cursors.get(userHash);
      if (c) return c;

      const meta = userMetaRef.current.get(userHash);
      const color = meta?.color ?? colorsRef.current.cursorFallback;
      const name = meta?.name ?? "?";

      const group = new Konva.Group({ x: 0, y: 0, visible: false });

      const arrow = new Konva.Line({
        points: [0, 0, 3.6, 13.5, 6.9, 9.7, 13.3, 7.2],
        fill: color,
        closed: true,
        stroke: colorsRef.current.cursorOutline,
        strokeWidth: 0.5,
      });

      const labelGroup = new Konva.Group({ x: 12, y: 14 });
      const labelText = new Konva.Text({
        text: name,
        fontSize: 11,
        fill: colorsRef.current.cursorLabelText,
        x: 4,
        y: 2,
        fontFamily: "system-ui, sans-serif",
      });

      const labelBg = new Konva.Rect({
        width: Math.ceil(labelText.getTextWidth()) + 8,
        height: 16,
        fill: color,
        cornerRadius: 3,
      });
      labelGroup.add(labelBg, labelText);
      group.add(arrow, labelGroup);
      layer!.add(group);

      const spring = createSpring(0, 0);
      c = { group, spring };
      cursors.set(userHash, c);
      return c;
    }

    function startAnimation() {
      if (animatingRef.current) return;
      animatingRef.current = true;
      lastTimeRef.current = performance.now();
      rafRef.current = requestAnimationFrame(tick);
    }

    function tick(now: number) {
      const dt = (now - lastTimeRef.current) / 1000;
      lastTimeRef.current = now;

      let anyMoving = false;
      for (const [, c] of cursors) {
        if (tickSpring(c.spring, dt)) {
          anyMoving = true;
        }
        c.group.position({ x: c.spring.x, y: c.spring.y });
      }

      layer!.batchDraw();

      if (anyMoving) {
        rafRef.current = requestAnimationFrame(tick);
      } else {
        animatingRef.current = false;
      }
    }

    cursorWS.on({
      onCursorMove(userHash, x, y) {
        const c = ensureCursor(userHash);
        // If this is the first position, snap instead of animating
        if (!c.group.visible()) {
          c.spring.x = x;
          c.spring.y = y;
          c.spring.vx = 0;
          c.spring.vy = 0;
          c.group.visible(true);
        }
        setSpringTarget(c.spring, x, y);
        startAnimation();
      },
      onCursorLeave(userHash) {
        const c = cursors.get(userHash);
        if (c) {
          c.group.destroy();
          cursors.delete(userHash);
          layer!.batchDraw();
        }
      },
    });

    return () => {
      cancelAnimationFrame(rafRef.current);
      animatingRef.current = false;
      cursorWS.on({});
      for (const c of cursors.values()) {
        c.group.destroy();
      }
      cursors.clear();
    };
  }, [cursorWS]);

  return <Layer ref={layerRef} listening={false} />;
}
