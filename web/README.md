# CedarLogic on the web

Not a reimplementation. The gates, the wires, the geometry, the collision
boxes, the routing and the drawing are the same C++ that builds the desktop
app, compiled to WebAssembly. This directory is the shell around it: a canvas,
an event loop, and the chrome.

## Why this works

Three seams already existed in the desktop code, and the port is mostly a
matter of standing on them.

**`cl::render::Scene`** (`include/gui/render/Scene.h`) is an engine-neutral
primitive sink. Every draw site emits into it -- `guiGate::drawToScene`,
`guiWire::drawToScene`, `CircuitPage::renderToScene` -- and no OpenGL or Skia
type crosses it. Skia implements it on the desktop; `SceneBuffer` implements it
here, recording a frame as one packed float32 stream that `src/scene.ts`
replays onto Canvas2D. Neither renderer decides how a circuit looks. The
geometry does, once.

**`render/TextMetrics.h`** is the same idea for measurement. Layout code that
boxes a label has to measure it the way it will be drawn, so the measurement is
a seam too: Skia answers on the desktop, `measureText` answers here.

**`klsCommand::toString()` and `cmd::fromLine()`** mean every edit is already a
serializable, replayable command. Nothing in this directory uses that yet, but
it is the reason peer-to-peer editing is a transport problem rather than a
rewrite.

## Layout

    wasm/gui/          the core, built for the browser
      shim/            the wx vocabulary the core still spells, and GL scalars
      SceneBuffer.h    Scene -> packed float32 command stream
      bindings.cpp     the embind boundary
      headless.cpp     the desktop-only methods, answered for a browser
      text_metrics.cpp the TextMetrics seam, via Canvas2D
    web/src/
      scene.ts         the other half of SceneBuffer's wire format
      engine.ts        loading and typing the wasm module
      main.ts          the shell

`shim/` is a naming layer, not a toolkit. When a newly added core file fails to
compile, the fix is almost always to break an include in `src/gui` rather than
to widen the shim.

## Running it

    ./wasm/gui/build.sh    # needs emscripten; writes web/public/engine/
    cd web && bun install && bun run dev

## What is here, and what is not

Working: the gate library parses, gates are created through the real
`GUICircuit::createGate`, and a page renders through the real
`CircuitPage::renderToScene`. Text, fills, arcs, and the grid all come out of
the shared code.

Not yet:

- **Loading `.cdl` files.** `format/` compiles and `cl::loadCircuit` works, but
  turning a `CircuitFile` into gates lives in `CircuitParse::applyCircuitFile`,
  which still takes `GUICanvas*`. Retargeting it at `CircuitPage*` is the next
  cut, and it makes save work too.
- **Input.** Pan, zoom, selection, and dragging live in `GUICanvas`, which is
  still a wxGLCanvas. The interaction logic is toolkit-independent in substance
  but not yet in form.
- **Simulation.** The logic core is linked and the message pump compiles, but
  nothing drives it here; the desktop runs it on a second thread, and the
  browser wants a different shape (a worker, or stepping from the frame loop).
- **Multiplayer.** See the command seam above.
