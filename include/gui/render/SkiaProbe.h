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
struct Transform;

// Point the font search at the app's resources root (a path ending in '/'), so a
// bundled face is found wherever the app was installed rather than relative to
// whatever directory it happened to be launched from. Call before the first
// render; unset simply means no bundled face is looked for.
void setFontSearchDir(const char* dir);

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

// Render a scene into an offscreen raster surface (cleared to white) and copy
// the result out as tightly packed, top-down RGB -- 3 bytes per pixel, so the
// caller must provide width*height*3 bytes. Lets non-Skia code (bitmap export,
// the clipboard, palette thumbnails) get pixels without a temporary file and
// without pulling in Skia headers. Returns false if the surface or readback
// failed.
bool skiaRenderToRGB(int width, int height,
                     const std::function<void(Scene&)>& draw,
                     unsigned char* outRgb);

// Render a scene straight into the currently-bound window framebuffer via Skia's
// Ganesh GL backend, then flush (the caller presents with SwapBuffers). The GL
// context must be current. `fboId` is the target framebuffer (0 = window).
// This is the live on-screen path (Workstream G3); returns false if Skia could
// not adopt the GL context or wrap the framebuffer.
bool skiaRenderWindow(int width, int height, int fboId,
                      const std::function<void(Scene&)>& draw,
                      float strokeScale = 1.0f);

// Like skiaRenderWindow, but caches the `drawStatic` layer as an image keyed by
// `contentKey`: when the key (and size) are unchanged the cached image is
// re-blitted instead of re-running drawStatic, and only `drawOverlay` is redrawn
// on top. Built for the minimap, where panning the main canvas moves only the
// viewport rectangle (the overlay) while the circuit thumbnail (the static
// layer) is unchanged -- so a pan costs a blit, not a full circuit redraw.
bool skiaRenderWindowCached(int width, int height, int fboId,
                            unsigned long long contentKey,
                            const std::function<void(Scene&)>& drawStatic,
                            const std::function<void(Scene&)>& drawOverlay,
                            float strokeScale = 1.0f);

// The live main-canvas path with retained rendering. The circuit (drawScene) is
// recorded once into an SkPicture in world coordinates and re-recorded only when
// `sceneKey` or the camera SCALE changes -- so a pan (translation only) is a
// picture replay, not a full re-run of drawToScene over every gate/wire. The
// picture is recorded under the camera's scale (so device-pixel stroke widths
// bake correctly) and replayed under its translation. `drawGrid` is camera-
// dependent (fills the viewport) so it's drawn live every frame, on top of the
// cleared surface and under the circuit. `camera` is the full world->device
// transform; `drawGrid` sets its own viewport, `drawScene` must NOT (the seam
// sets the recording matrix).
// `drawOverlay` (optional) is drawn LIVE on top of the replayed circuit picture
// every frame -- it must NOT be folded into `sceneKey`. This keeps interactive,
// per-mouse-move overlays (hover bulbs, drag boxes, wire hover) out of the cached
// picture, so moving the mouse replays the circuit instead of re-recording it.
// Like `drawGrid`, it runs against the full camera and sets its own viewport.
bool skiaRenderWindowScene(int width, int height, int fboId,
                           unsigned long long sceneKey,
                           const Transform& camera,
                           const std::function<void(Scene&)>& drawGrid,
                           const std::function<void(Scene&)>& drawScene,
                           const std::function<void(Scene&)>& drawOverlay = {});

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

// Advance width of a UTF-8 string as the Skia text path (Scene::text) renders it
// at `pixelHeight`, using the same font. Lets non-Skia layout code (a label's hit
// box) size itself to what actually renders instead of the GL font's metrics.
// Returns 0 if no font is available. Keeps Skia headers out of the caller.
float measuredTextWidth(const char* utf8, float pixelHeight);

// The height that same text occupies: top of the capitals down past the
// descenders, so a caller can box exactly what gets drawn.
float measuredTextHeight(float pixelHeight);

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
#endif  // CL_RENDER_SKIAPROBE_H
