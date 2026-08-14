// SkiaScene -- Scene primitives -> Skia draw calls. See SkiaScene.h.

#ifdef WITH_SKIA

#include "render/SkiaScene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/SkCanvas.h"
#include "core/SkColor.h"
#include "core/SkFont.h"
#include "core/SkFontMetrics.h"
#include "core/SkFontTypes.h"
#include "core/SkMatrix.h"
#include "core/SkPaint.h"
#include "core/SkPath.h"
#include "core/SkPathEffect.h"
#include "core/SkRect.h"
#include "effects/SkDashPathEffect.h"

namespace cl {
namespace render {
namespace {

SkColor4f toColor(const Color& c) {
	return SkColor4f{c.r, c.g, c.b, c.a};
}

SkMatrix toMatrix(const Transform& t) {
	// Scene transform is [a c e ; b d f]; SkMatrix::setAll takes rows.
	SkMatrix m;
	m.setAll(t.a, t.c, t.e,
	         t.b, t.d, t.f,
	         0.0f, 0.0f, 1.0f);
	return m;
}

SkPaint::Cap toCap(Cap c) {
	switch (c) {
		case Cap::Round:  return SkPaint::kRound_Cap;
		case Cap::Square: return SkPaint::kSquare_Cap;
		case Cap::Butt:
		default:          return SkPaint::kButt_Cap;
	}
}

// Uniform scale factor of a canvas's current matrix (sqrt of |determinant|).
// Used to keep stroke widths in device pixels regardless of the viewport zoom,
// matching GL's device-space glLineWidth.
float deviceScale(SkCanvas* canvas) {
	SkMatrix m = canvas->getTotalMatrix();
	float det = m.getScaleX() * m.getScaleY() - m.getSkewX() * m.getSkewY();
	float s = std::sqrt(std::fabs(det));
	return s > 1e-6f ? s : 1.0f;
}

SkPaint strokePaint(const Stroke& s, float devScale) {
	SkPaint p;
	p.setAntiAlias(true);
	p.setStyle(SkPaint::kStroke_Style);
	p.setStrokeWidth(s.width / devScale);   // s.width is device pixels
	p.setStrokeCap(toCap(s.cap));
	p.setColor4f(toColor(s.color));
	if (s.dashed) {
		// Approximates the legacy 0x9999 selection stipple (device-space dashes).
		const SkScalar intervals[] = {3.0f / devScale, 3.0f / devScale};
		p.setPathEffect(SkDashPathEffect::Make(intervals, 2, 0.0f));
	}
	return p;
}

SkPaint fillPaint(const Color& c) {
	SkPaint p;
	p.setAntiAlias(true);
	p.setStyle(SkPaint::kFill_Style);
	p.setColor4f(toColor(c));
	return p;
}

}  // namespace

SkiaScene::SkiaScene(SkCanvas* canvas, const SkFont* font, float strokeScale)
	: fCanvas(canvas), fFont(font),
	  fStrokeScale(strokeScale > 1e-3f ? strokeScale : 1.0f) {}

float SkiaScene::strokeDevScale() const {
	return deviceScale(fCanvas) / fStrokeScale;
}

void SkiaScene::setViewport(const Transform& worldToDevice) {
	fCanvas->setMatrix(toMatrix(worldToDevice));
}

void SkiaScene::pushTransform(const Transform& local) {
	fCanvas->save();
	fCanvas->concat(toMatrix(local));
}

void SkiaScene::popTransform() {
	fCanvas->restore();
}

void SkiaScene::lines(const Point* pts, std::size_t count, const Stroke& s) {
	if (count < 2) return;
	SkPath path;
	// GL_LINES semantics: disconnected pairs.
	for (std::size_t i = 0; i + 1 < count; i += 2) {
		path.moveTo(pts[i].x, pts[i].y);
		path.lineTo(pts[i + 1].x, pts[i + 1].y);
	}
	fCanvas->drawPath(path, strokePaint(s, strokeDevScale()));
}

void SkiaScene::polyline(const Point* pts, std::size_t count, const Stroke& s,
                         bool closed) {
	if (count < 2) return;
	SkPath path;
	path.moveTo(pts[0].x, pts[0].y);
	for (std::size_t i = 1; i < count; ++i) path.lineTo(pts[i].x, pts[i].y);
	if (closed) path.close();
	fCanvas->drawPath(path, strokePaint(s, strokeDevScale()));
}

void SkiaScene::fillPolygon(const Point* pts, std::size_t count,
                            const Color& c) {
	if (count < 3) return;
	SkPath path;
	path.moveTo(pts[0].x, pts[0].y);
	for (std::size_t i = 1; i < count; ++i) path.lineTo(pts[i].x, pts[i].y);
	path.close();
	fCanvas->drawPath(path, fillPaint(c));
}

void SkiaScene::fillCircle(Point center, float radius, const Color& c) {
	fCanvas->drawCircle(center.x, center.y, radius, fillPaint(c));
}

void SkiaScene::strokeCircle(Point center, float radius, const Stroke& s) {
	fCanvas->drawCircle(center.x, center.y, radius, strokePaint(s, strokeDevScale()));
}

void SkiaScene::arc(Point center, float radius, float startDeg, float sweepDeg,
                    const Stroke& s) {
	// True stroked arc -- no chording, crisp at any zoom. The Scene convention is
	// degrees from +Y increasing clockwise toward +X; Skia's addArc measures from
	// +X increasing clockwise (device space), so start -> 90 - start and the sweep
	// flips sign. Points match the base tessellation and the circle convention.
	const SkRect oval = SkRect::MakeLTRB(center.x - radius, center.y - radius,
	                                     center.x + radius, center.y + radius);
	SkPath path;
	if (std::fabs(sweepDeg) >= 360.0f) {
		// A whole turn is a circle, and must be asked for as one. Skia only
		// nudges the start and stop vectors apart for sweeps just UNDER a full
		// turn; at exactly 360 they stay coincident and the arc emits no
		// geometry at all -- whether it does is down to how the start angle
		// rounds, so of the eight -360 bubbles on a NANDX8 four drew and four
		// vanished. The gate library has 17 of these.
		path.addOval(oval);
	} else {
		path.addArc(oval, 90.0f - startDeg, -sweepDeg);
	}
	fCanvas->drawPath(path, strokePaint(s, strokeDevScale()));
}

void SkiaScene::fillRect(Point lo, Point hi, const Color& c) {
	const SkRect r = SkRect::MakeLTRB(std::min(lo.x, hi.x), std::min(lo.y, hi.y),
	                                  std::max(lo.x, hi.x), std::max(lo.y, hi.y));
	fCanvas->drawRect(r, fillPaint(c));
}

void SkiaScene::text(Point origin, const char* utf8, float pixelHeight,
                     const Color& c) {
	if (!fFont || !utf8 || !*utf8) return;

	// Build the outlines at kGlyphUnits and scale them into place, rather than
	// asking the font for `pixelHeight` directly: at the world-unit sizes labels
	// request (~2.7) the font grid-fits the outlines into mush and rounds every
	// advance to zero, stacking the whole string on one spot.
	const float k = pixelHeight / kGlyphUnits;
	SkFont f(*fFont);
	f.setSize(kGlyphUnits);
	f.setHinting(SkFontHinting::kNone);   // outlines, not grid-fitted bitmaps
	f.setLinearMetrics(true);             // true fractional advances

	// Render the label as vector glyph paths rather than atlas glyphs. Schematic
	// text is drawn through a live zoom, and the GPU glyph atlas re-buckets on
	// every size change -- briefly scaling a cached glyph, which reads as flashing
	// stretched text. Paths scale exactly with the canvas matrix, so text stays
	// crisp and artifact-free at any zoom (there are few labels, so cost is low).
	const size_t len = std::strlen(utf8);
	const int n = f.countText(utf8, len, SkTextEncoding::kUTF8);
	if (n <= 0) return;
	std::vector<SkGlyphID> glyphs(n);
	f.textToGlyphs(utf8, len, SkTextEncoding::kUTF8, glyphs.data(), n);
	std::vector<SkScalar> xpos(n);
	f.getXPos(glyphs.data(), n, xpos.data(), 0.0f);
	SkPath text, glyph;
	for (int i = 0; i < n; i++) {
		if (f.getPath(glyphs[i], &glyph)) {
			glyph.offset(xpos[i], 0.0f);
			text.addPath(glyph);
		}
	}

	SkPaint p = fillPaint(c);
	// The retired GL bitmap font anchored near the TOP of the text -- glyphs hang
	// below the draw point, and label hit boxes (TEXT_BOX_TOP/BOTTOM in guiGate.h)
	// still assume that. SkPath glyphs are baseline-relative, so anchoring at the baseline would
	// render the text a whole line-height too high, off its hit box. Drop the
	// baseline by the ascent so the top of the text sits at the origin, matching
	// the GL font and the hit box.
	SkFontMetrics fm;
	f.getMetrics(&fm);
	// The viewport flips y (device is y-down); flip it back around the text origin
	// so glyphs render upright rather than mirrored.
	fCanvas->save();
	fCanvas->translate(origin.x, origin.y + fm.fAscent * k);  // fAscent < 0 (above baseline)
	fCanvas->scale(k, -k);
	fCanvas->drawPath(text, p);
	fCanvas->restore();
}

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
