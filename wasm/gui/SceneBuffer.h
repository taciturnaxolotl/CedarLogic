// SceneBuffer -- a cl::render::Scene that records into a flat float array.
//
// The browser draws with Canvas2D, which lives on the JavaScript side of the
// wasm boundary. Calling out per primitive would cost a JS call per line of the
// schematic, so instead a frame is recorded here as one packed command stream
// and handed over as a single typed-array view. JavaScript then replays it in a
// tight switch. One boundary crossing per frame, no allocation per primitive.
//
// The stream is float32 throughout, including opcodes, counts, and string
// handles -- mixing types would cost more in alignment than it saves in space,
// and every value here is small enough to survive a float exactly.
//
// Layout is `[op, payload...]` repeated. See Op below; `replay()` in
// web/src/scene.ts is the other half of this contract and must move with it.

#ifndef CEDAR_WASM_SCENEBUFFER_H
#define CEDAR_WASM_SCENEBUFFER_H

#include <string>
#include <vector>

#include "render/Scene.h"

namespace cl {
namespace wasm {

class SceneBuffer : public render::Scene {
public:
	enum Op {
		kSetViewport  = 0,   // a b c d e f
		kPushTransform = 1,  // a b c d e f
		kPopTransform = 2,   // -
		kLines        = 3,   // stroke(7) n  x0 y0 ... (n points, pairwise)
		kPolyline     = 4,   // stroke(7) closed n  x0 y0 ...
		kFillPolygon  = 5,   // rgba(4) n  x0 y0 ...
		kFillCircle   = 6,   // rgba(4) cx cy radius
		kStrokeCircle = 7,   // stroke(7) cx cy radius
		kFillRect     = 8,   // rgba(4) loX loY hiX hiY
		kText         = 9,   // rgba(4) x y pixelHeight stringHandle
		kArc          = 10,  // stroke(7) cx cy radius startDeg sweepDeg
	};

	void clear() {
		fCmds.clear();
		fStrings.clear();
	}

	// The recorded frame. Valid until the next clear()/draw; JavaScript wraps it
	// as a Float32Array view over wasm memory rather than copying.
	const float* data() const { return fCmds.empty() ? nullptr : &fCmds[0]; }
	std::size_t size() const { return fCmds.size(); }

	// Strings referenced by kText, in handle order.
	const std::vector<std::string>& strings() const { return fStrings; }

	// --- cl::render::Scene -------------------------------------------------

	void setViewport(const render::Transform& t) override {
		emit(kSetViewport);
		transform(t);
	}

	void pushTransform(const render::Transform& t) override {
		emit(kPushTransform);
		transform(t);
	}

	void popTransform() override { emit(kPopTransform); }

	void lines(const render::Point* pts, std::size_t count,
	           const render::Stroke& s) override {
		emit(kLines);
		stroke(s);
		points(pts, count);
	}

	void polyline(const render::Point* pts, std::size_t count,
	              const render::Stroke& s, bool closed) override {
		emit(kPolyline);
		stroke(s);
		emit(closed ? 1.0f : 0.0f);
		points(pts, count);
	}

	void fillPolygon(const render::Point* pts, std::size_t count,
	                 const render::Color& c) override {
		emit(kFillPolygon);
		color(c);
		points(pts, count);
	}

	void fillCircle(render::Point center, float radius,
	                const render::Color& c) override {
		emit(kFillCircle);
		color(c);
		emit(center.x); emit(center.y); emit(radius);
	}

	void strokeCircle(render::Point center, float radius,
	                  const render::Stroke& s) override {
		emit(kStrokeCircle);
		stroke(s);
		emit(center.x); emit(center.y); emit(radius);
	}

	// Canvas2D strokes a true arc, so pass it through rather than letting the
	// base class tessellate to a 64-segment polyline. Curves stay smooth at any
	// zoom and the command stream stays short.
	void arc(render::Point center, float radius, float startDeg, float sweepDeg,
	         const render::Stroke& s) override {
		emit(kArc);
		stroke(s);
		emit(center.x); emit(center.y); emit(radius);
		emit(startDeg); emit(sweepDeg);
	}

	void fillRect(render::Point lo, render::Point hi,
	              const render::Color& c) override {
		emit(kFillRect);
		color(c);
		emit(lo.x); emit(lo.y); emit(hi.x); emit(hi.y);
	}

	void text(render::Point origin, const char* utf8, float pixelHeight,
	          const render::Color& c) override {
		emit(kText);
		color(c);
		emit(origin.x); emit(origin.y); emit(pixelHeight);
		fStrings.push_back(utf8 ? utf8 : "");
		emit((float)(fStrings.size() - 1));
	}

private:
	void emit(float v) { fCmds.push_back(v); }

	void color(const render::Color& c) {
		emit(c.r); emit(c.g); emit(c.b); emit(c.a);
	}

	void stroke(const render::Stroke& s) {
		color(s.color);
		emit(s.width);
		emit((float)(int)s.cap);
		emit(s.dashed ? 1.0f : 0.0f);
	}

	void transform(const render::Transform& t) {
		emit(t.a); emit(t.b); emit(t.c); emit(t.d); emit(t.e); emit(t.f);
	}

	void points(const render::Point* pts, std::size_t count) {
		emit((float)count);
		fCmds.reserve(fCmds.size() + count * 2);
		for (std::size_t i = 0; i < count; i++) {
			emit(pts[i].x);
			emit(pts[i].y);
		}
	}

	std::vector<float> fCmds;
	std::vector<std::string> fStrings;
};

}  // namespace wasm
}  // namespace cl

#endif  // CEDAR_WASM_SCENEBUFFER_H
