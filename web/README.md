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

**`CanvasCamera`** is the same idea applied to feel rather than looks. Where
the view sits, how far a wheel notch zooms, whether a zoom pivots on the cursor,
where a point lands when it snaps -- all arithmetic, none of it needing a
window. `klsGLCanvas` keeps one rather than being one, and so does the browser
document, which is why panning and zooming here are identical rather than
merely similar. `src/gui/tests/test_camera.cpp` pins the numbers.

**`input::PointerEvent` and `input::KeyEvent`** carry input with no toolkit
attached, so the canvas handlers describing what a click selects and what starts
a drag can be compiled rather than reimplemented. `klsGLCanvas` is the only
place wx is translated.

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

**`PageHost`** is what the interaction asks of whatever surface it runs on:
where the pointer is, whether a drag is under way, when to repaint, how to open
a picker, whether editing is locked. `GUICanvas` answers it from wxWidgets in
about twenty lines; the browser document answers it from DOM events. That is
the whole of the difference between the two shells' interaction.

## What is here

Open and save `.cdl` (legacy v1 and v2 files migrate on the way in, through the
desktop's own reader). Select by click or rubber band, drag, rotate, delete,
copy, cut, paste, undo and redo. Pan, zoom, zoom-to-fit. The simulation runs,
toggles respond, and wires show their live state.

All of it is the desktop's code. The numbers that make up the feel came across
unchanged: a wheel notch scales by exactly 0.75, the point under the cursor does
not drift, and the 85 ms click-versus-drag dead zone still turns a quick drag
into a click.

## What is not

- **Multiple pages.** The desktop has tabs; here everything lands on one page.
  `CircuitParse` already asks a `PageProvider` for page N, so the shell needs
  tabs rather than the core needing changes.
- **The oscilloscope.** `CircuitObserver::oscopeDataAdded` fires here and goes
  nowhere.
- **Parameter dialogs.** `requestQuickAdd` focuses the palette filter; a
  double-click that would open a parameter dialog on the desktop does nothing.
- **The system clipboard.** Copy and paste work within the page. The browser's
  clipboard is asynchronous and gated on a gesture, so `clipboardText` and
  `setClipboardText` bridge to it around one.
- **Multiplayer.** See the command seam above. Still just an observation.
