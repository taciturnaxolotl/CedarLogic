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

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
#endif  // CL_RENDER_SKIAPROBE_H
