// SkiaScene -- the Skia backend for the rendering seam (Workstream G).
//
// Implements Scene by translating engine-neutral primitives into Skia draw
// calls on an SkCanvas. The canvas may be GPU-backed (screen), raster (offscreen
// PNG), SVG (SkSVGCanvas), or PDF (SkPDF) -- the same SkiaScene drives all of
// them, which is how one recorded scene fans out to every output.
//
// Compiles only under WITH_SKIA; the seam header (Scene.h) it implements is
// always available so draw sites never depend on Skia directly.

#ifndef CL_RENDER_SKIASCENE_H
#define CL_RENDER_SKIASCENE_H

#ifdef WITH_SKIA

#include "Scene.h"

class SkCanvas;
class SkFont;

namespace cl {
namespace render {

class SkiaScene : public Scene {
public:
	// Neither pointer is owned; the caller keeps both alive for the SkiaScene's
	// lifetime. `font` supplies the typeface for text() (its size is overridden
	// per call from the requested pixel height); may be null to skip text.
	// `strokeScale` multiplies every stroke width (default 1.0); the minimap uses
	// <1 so a whole circuit shrunk to a thumbnail stays hairline instead of
	// collapsing dense line clusters into a black blob.
	SkiaScene(SkCanvas* canvas, const SkFont* font, float strokeScale = 1.0f);

	void setViewport(const Transform& worldToDevice) override;
	void pushTransform(const Transform& local) override;
	void popTransform() override;
	void lines(const Point* pts, std::size_t count, const Stroke&) override;
	void polyline(const Point* pts, std::size_t count, const Stroke&,
	              bool closed) override;
	void fillPolygon(const Point* pts, std::size_t count, const Color&) override;
	void fillCircle(Point center, float radius, const Color&) override;
	void strokeCircle(Point center, float radius, const Stroke&) override;
	void fillRect(Point lo, Point hi, const Color&) override;
	void text(Point origin, const char* utf8, float pixelHeight,
	          const Color&) override;

	// Keep the base's std::vector convenience overloads visible (the virtual
	// overrides above would otherwise hide them by name).
	using Scene::lines;
	using Scene::polyline;
	using Scene::fillPolygon;

private:
	// Effective device scale for stroke widths: the canvas scale divided by
	// fStrokeScale, so widths render at (device width * fStrokeScale) px.
	float strokeDevScale() const;

	SkCanvas* fCanvas;
	const SkFont* fFont;
	float fStrokeScale;
};

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
#endif  // CL_RENDER_SKIASCENE_H
