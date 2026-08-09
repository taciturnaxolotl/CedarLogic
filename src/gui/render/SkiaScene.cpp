// SkiaScene -- Scene primitives -> Skia draw calls. See SkiaScene.h.

#ifdef WITH_SKIA

#include "render/SkiaScene.h"

#include <algorithm>
#include <cmath>

#include "core/SkCanvas.h"
#include "core/SkColor.h"
#include "core/SkFont.h"
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

SkiaScene::SkiaScene(SkCanvas* canvas, const SkFont* font)
	: fCanvas(canvas), fFont(font) {}

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
	fCanvas->drawPath(path, strokePaint(s, deviceScale(fCanvas)));
}

void SkiaScene::polyline(const Point* pts, std::size_t count, const Stroke& s,
                         bool closed) {
	if (count < 2) return;
	SkPath path;
	path.moveTo(pts[0].x, pts[0].y);
	for (std::size_t i = 1; i < count; ++i) path.lineTo(pts[i].x, pts[i].y);
	if (closed) path.close();
	fCanvas->drawPath(path, strokePaint(s, deviceScale(fCanvas)));
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
	fCanvas->drawCircle(center.x, center.y, radius, strokePaint(s, deviceScale(fCanvas)));
}

void SkiaScene::fillRect(Point lo, Point hi, const Color& c) {
	const SkRect r = SkRect::MakeLTRB(std::min(lo.x, hi.x), std::min(lo.y, hi.y),
	                                  std::max(lo.x, hi.x), std::max(lo.y, hi.y));
	fCanvas->drawRect(r, fillPaint(c));
}

void SkiaScene::text(Point origin, const char* utf8, float pixelHeight,
                     const Color& c) {
	if (!fFont || !utf8) return;
	SkFont f(*fFont);
	f.setSize(pixelHeight);
	SkPaint p = fillPaint(c);
	fCanvas->drawString(utf8, origin.x, origin.y, f, p);
}

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
