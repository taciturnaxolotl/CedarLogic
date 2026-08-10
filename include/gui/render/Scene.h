// Scene -- the rendering seam for Workstream G.
//
// Today CedarLogic draws its schematic twice: immediate-mode OpenGL 1.x on
// screen (guiGate::draw / guiWire::draw / guiText::draw emit glBegin per frame)
// and a separate hand-written SVG emitter for export. The seam collapses both:
// every draw site emits engine-neutral primitives into a Scene, and one backend
// (Skia, behind WITH_SKIA) records and replays them to the screen, to SVG, to
// PDF, and to PNG.
//
// This header is deliberately engine-agnostic -- no OpenGL, no Skia types cross
// it -- so the draw sites depend only on this vocabulary and a different backend
// (e.g. NanoVG) remains possible. It is plain C++11 and header-only, so it
// compiles in the default build with zero impact until draw sites are ported
// (phase G1).
//
// Coordinates are world units (the same space gates/wires already live in); the
// viewport transform maps world -> device, and per-object transforms (a gate's
// model matrix) are pushed/popped around its primitives.

#ifndef CL_RENDER_SCENE_H
#define CL_RENDER_SCENE_H

#include <cstddef>
#include <cmath>
#include <vector>

namespace cl {
namespace render {

// Straight RGBA, components in [0,1].
struct Color {
	float r, g, b, a;
	Color() : r(0), g(0), b(0), a(1) {}
	Color(float r_, float g_, float b_, float a_ = 1.0f)
		: r(r_), g(g_), b(b_), a(a_) {}
};

struct Point {
	float x, y;
	Point() : x(0), y(0) {}
	Point(float x_, float y_) : x(x_), y(y_) {}
};

// 2D affine transform as [a c e ; b d f], i.e.
//   x' = a*x + c*y + e
//   y' = b*x + d*y + f
// This is what a gate's model matrix (currently a GLdouble[16]) reduces to in
// the plane, and what the viewport (pan/zoom) uses.
struct Transform {
	float a, b, c, d, e, f;
	Transform() : a(1), b(0), c(0), d(1), e(0), f(0) {}  // identity
	static Transform translate(float tx, float ty) {
		Transform t; t.e = tx; t.f = ty; return t;
	}
	static Transform scale(float sx, float sy) {
		Transform t; t.a = sx; t.d = sy; return t;
	}
};

enum class Cap { Butt, Round, Square };

// Stroke appearance. `dashed` carries the current selection stipple; the
// backend chooses the concrete dash pattern.
struct Stroke {
	Color color;
	float width;
	Cap cap;
	bool dashed;
	Stroke() : width(1.0f), cap(Cap::Butt), dashed(false) {}
	explicit Stroke(const Color& c, float w = 1.0f)
		: color(c), width(w), cap(Cap::Butt), dashed(false) {}
};

// The primitive sink. Draw sites call these; a backend records/replays them.
// Primitive set is exactly what the existing GL draw sites need:
//   - lines():       GL_LINES pairs (gate bodies, labels, wire segments, grid)
//   - polyline():    connected/closed outlines (TOGGLE box, LED outline)
//   - fillPolygon(): triangle-fan style fills (LED body, connection dots)
//   - fillCircle()/strokeCircle(): junction dots, bus end caps
//   - fillRect():    overlaps, drag-select rectangle
//   - text():        labels and TO/FROM captions
class Scene {
public:
	virtual ~Scene() {}

	// Camera. Maps world coordinates to device pixels (replaces gluOrtho2D +
	// glViewport). Pan/zoom is a change to this transform, not a scene rebuild.
	virtual void setViewport(const Transform& worldToDevice) = 0;

	// Per-object model transforms, applied on top of the viewport. push before
	// a gate's primitives, pop after (replaces glLoadMatrixd(mModel)).
	virtual void pushTransform(const Transform& local) = 0;
	virtual void popTransform() = 0;

	// Disconnected segments: pts[0]-pts[1], pts[2]-pts[3], ... (GL_LINES).
	virtual void lines(const Point* pts, std::size_t count, const Stroke&) = 0;

	// Connected path through pts; `closed` joins last->first (GL_LINE_STRIP /
	// GL_LINE_LOOP).
	virtual void polyline(const Point* pts, std::size_t count,
	                      const Stroke&, bool closed) = 0;

	// Convex/simple polygon fill (GL_TRIANGLE_FAN / GL_POLYGON).
	virtual void fillPolygon(const Point* pts, std::size_t count,
	                         const Color&) = 0;

	virtual void fillCircle(Point center, float radius, const Color&) = 0;
	virtual void strokeCircle(Point center, float radius, const Stroke&) = 0;

	// Stroked circular arc. Angles are degrees measured from +Y (up), increasing
	// clockwise toward +X -- the same convention the gate geometry uses for
	// circles -- so an arc from 0 to +180 is the right-hand half of the circle.
	// The default tessellates to a polyline; a backend that strokes a true arc
	// (Skia) overrides this for resolution-independent curves at any zoom.
	virtual void arc(Point center, float radius, float startDeg, float sweepDeg,
	                 const Stroke& s) {
		const int segs = 64;
		const float k = 3.14159265358979323846f / 180.0f;
		std::vector<Point> pts;
		pts.reserve(segs + 1);
		for (int i = 0; i <= segs; i++) {
			float d = startDeg + sweepDeg * (float)i / (float)segs;
			pts.push_back(Point(center.x + radius * std::sin(d * k),
			                    center.y + radius * std::cos(d * k)));
		}
		polyline(&pts[0], pts.size(), s, false);
	}

	// Axis-aligned rectangle fill; lo is any corner, hi the opposite.
	virtual void fillRect(Point lo, Point hi, const Color&) = 0;

	// UTF-8 text, positioned at its baseline origin, `pixelHeight` tall in world
	// units. The backend owns font selection (a bundled face under Skia).
	virtual void text(Point origin, const char* utf8, float pixelHeight,
	                  const Color&) = 0;

	// std::vector convenience overloads (thin forwarders; keep draw sites tidy).
	void lines(const std::vector<Point>& p, const Stroke& s) {
		if (!p.empty()) lines(&p[0], p.size(), s);
	}
	void polyline(const std::vector<Point>& p, const Stroke& s, bool closed) {
		if (!p.empty()) polyline(&p[0], p.size(), s, closed);
	}
	void fillPolygon(const std::vector<Point>& p, const Color& c) {
		if (!p.empty()) fillPolygon(&p[0], p.size(), c);
	}
};

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_SCENE_H
