// TextMetrics -- what a string will measure once it is drawn.
//
// Layout code that positions text (a label's hit box, a TO/FROM caption) has to
// agree with whatever will actually rasterize it, or the box and the glyphs
// drift apart. So the measurement is a seam, like Scene is: declared here with
// no engine attached, implemented by whichever backend the target links.
// SkiaBackend.cpp answers on the desktop; the WebAssembly build measures through
// the browser's Canvas2D, which is what draws there.

#ifndef CL_RENDER_TEXTMETRICS_H
#define CL_RENDER_TEXTMETRICS_H

namespace cl {
namespace render {

// Advance width of a UTF-8 string rendered at `pixelHeight`, in the same font
// Scene::text() will use. Returns 0 if no font is available.
float measuredTextWidth(const char* utf8, float pixelHeight);

// The height that text occupies: top of the capitals down past the descenders,
// so a caller can box exactly what gets drawn.
float measuredTextHeight(float pixelHeight);

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_TEXTMETRICS_H
