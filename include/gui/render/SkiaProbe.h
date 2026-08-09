// SkiaProbe -- a headless, Skia-free entry point so non-Skia translation units
// (MainApp) can trigger a Skia render without pulling Skia headers (which would
// force C++17 and clash with windows.h). The implementation lives in a Skia TU.
//
// Only declared when WITH_SKIA is on.

#ifndef CL_RENDER_SKIAPROBE_H
#define CL_RENDER_SKIAPROBE_H

#ifdef WITH_SKIA

#include <functional>

namespace cl {
namespace render {

class Scene;  // engine-neutral sink; the callback draws into a Skia-backed one

// Render the G0 proof (a stroked path + a line of text) into an offscreen raster
// surface and write it to `path` as a PNG. No window and no GL context, so it
// runs headless (CI, no display). Returns true on success. This is the concrete
// evidence that Skia actually rasterizes on a given machine.
bool skiaProbeToPng(const char* path, int width, int height);

// Render an arbitrary scene into an offscreen raster surface (cleared to white)
// and write it as a PNG. `draw` receives a Skia-backed Scene to emit into (e.g.
// GUICanvas::renderToScene). Keeps Skia headers out of the caller's TU.
bool skiaRenderToPng(const char* path, int width, int height,
                     const std::function<void(Scene&)>& draw);

// Render a scene straight into the currently-bound window framebuffer via Skia's
// Ganesh GL backend, then flush (the caller presents with SwapBuffers). The GL
// context must be current. `fboId` is the target framebuffer (0 = window).
// This is the live on-screen path (Workstream G3); returns false if Skia could
// not adopt the GL context or wrap the framebuffer.
bool skiaRenderWindow(int width, int height, int fboId,
                      const std::function<void(Scene&)>& draw);

// Render a scene into a vector SVG at `path`. Same callback contract as
// skiaRenderToPng; output is resolution-independent through the same Scene seam
// that matches the GL golden, replacing the hand-rolled svgExport.cpp. Canvas is
// `width` x `height` and filled white. Returns true on success.
bool skiaRenderToSvg(const char* path, int width, int height,
                     const std::function<void(Scene&)>& draw);

// Render a scene into a single-page PDF at `path` -- resolution-independent
// vector output for print. Same callback contract as skiaRenderToPng.
//
// NB: this needs a Skia build with the PDF backend compiled in. The pinned
// aseprite-m124 dist currently links a stub SkPDF that returns null (its
// skia.lib carries no real PDF impl), so this returns false there until the
// dist is rebuilt with skia_enable_pdf actually taking effect. The app code is
// backend-ready; only the supplied library is missing the piece.
bool skiaRenderToPdf(const char* path, int width, int height,
                     const std::function<void(Scene&)>& draw);

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
#endif  // CL_RENDER_SKIAPROBE_H
