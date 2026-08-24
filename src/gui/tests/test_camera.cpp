// CanvasCamera -- the arithmetic behind pan, zoom, and snapping.
//
// These are feel tests. Each one pins a number a user would notice if it moved:
// how far one wheel notch zooms, whether the point under the cursor stays under
// the cursor, where a dropped gate lands. The desktop and the browser share this
// class precisely so they cannot disagree about any of it, and these tests are
// what stop the shared answer from drifting.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "CanvasCamera.h"

namespace {

// A camera needs a viewport to answer most questions. This is the smallest
// host that can give it one.
class FixedViewport : public CameraHost {
public:
	FixedViewport(int w, int h) : fW(w), fH(h) {}
	int cameraViewportWidth() const override { return fW; }
	int cameraViewportHeight() const override { return fH; }
	void cameraRepaint() override { repaints++; }
	void cameraPointerFollowed() override { pointerFollows++; }

	int repaints = 0;
	int pointerFollows = 0;

private:
	int fW, fH;
};

}  // namespace

TEST_CASE("a fresh camera sits at the origin at the default zoom") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);

	GLdouble x, y;
	cam.getPan(x, y);
	CHECK(x == doctest::Approx(0.0));
	CHECK(y == doctest::Approx(0.0));
	CHECK(cam.getZoom() == doctest::Approx(DEFAULT_ZOOM));
}

TEST_CASE("the viewport is the window measured in world units") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);
	cam.setPan(10.0, 20.0);

	GLPoint2f topLeft, bottomRight;
	cam.getViewport(topLeft, bottomRight);

	// Pan names the top-left corner; y grows upward, so the bottom is below it.
	CHECK(topLeft.x == doctest::Approx(10.0));
	CHECK(topLeft.y == doctest::Approx(20.0));
	CHECK(bottomRight.x == doctest::Approx(10.0 + 800 * DEFAULT_ZOOM));
	CHECK(bottomRight.y == doctest::Approx(20.0 - 600 * DEFAULT_ZOOM));
}

TEST_CASE("zoom is clamped to the allowed range") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);

	cam.setZoom(1000.0);
	CHECK(cam.getZoom() == doctest::Approx(MAX_ZOOM));

	cam.setZoom(0.0000001);
	CHECK(cam.getZoom() == doctest::Approx(MIN_ZOOM));
}

TEST_CASE("zooming holds the centre of the view still") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);
	cam.setPan(-40.0, 30.0);

	const GLPoint2f before = cam.getCenter();
	cam.setZoom(cam.getZoom() * ZOOM_STEP);
	const GLPoint2f after = cam.getCenter();

	CHECK(after.x == doctest::Approx(before.x));
	CHECK(after.y == doctest::Approx(before.y));
}

TEST_CASE("one wheel notch scales by exactly one zoom step") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);
	const GLdouble start = cam.getZoom();

	cam.zoomToMouse(1, cam.getCenter());
	CHECK(cam.getZoom() == doctest::Approx(start * ZOOM_STEP));

	cam.zoomToMouse(-1, cam.getCenter());
	CHECK(cam.getZoom() == doctest::Approx(start));
}

TEST_CASE("zooming to the mouse keeps the point under the cursor") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);

	// A point well off-centre, so a centre-anchored zoom would visibly move it.
	const GLPoint2f cursor = cam.mapToWorld(700, 100);

	cam.zoomToMouse(3, cursor);

	// The same screen pixel must still name the same world point.
	const GLPoint2f after = cam.mapToWorld(700, 100);
	CHECK(after.x == doctest::Approx(cursor.x).epsilon(0.001));
	CHECK(after.y == doctest::Approx(cursor.y).epsilon(0.001));
}

TEST_CASE("a compound move repaints once, at the final state") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);
	host.repaints = 0;

	// A zoom is a setZoom plus a setCenter, each of which pans. Without the
	// bracket the host would paint the intermediate, centre-anchored frame.
	cam.zoomToMouse(1, cam.mapToWorld(700, 100));
	CHECK(host.repaints == 1);
}

TEST_CASE("panning tells the host the pointer's world position moved") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);
	host.pointerFollows = 0;

	cam.translatePan(5.0, 5.0);
	CHECK(host.pointerFollows == 1);
}

TEST_CASE("pan is clamped so the view cannot run away") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);

	cam.setPan(1.0e20, -1.0e20);
	GLdouble x, y;
	cam.getPan(x, y);
	CHECK(x == doctest::Approx(MAX_PAN));
	CHECK(y == doctest::Approx(MIN_PAN));
}

TEST_CASE("points snap to the nearest grid intersection") {
	FixedViewport host(800, 600);
	CanvasCamera cam(&host);
	cam.setGridSpacing(1.0f, 1.0f);

	CHECK(cam.getSnappedPoint(GLPoint2f(2.4f, 7.6f)).x == doctest::Approx(2.0f));
	CHECK(cam.getSnappedPoint(GLPoint2f(2.4f, 7.6f)).y == doctest::Approx(8.0f));

	// Halfway rounds up, and negatives round the same direction.
	CHECK(cam.getSnappedPoint(GLPoint2f(2.5f, -2.5f)).x == doctest::Approx(3.0f));
	CHECK(cam.getSnappedPoint(GLPoint2f(2.5f, -2.5f)).y == doctest::Approx(-2.0f));
}

TEST_CASE("fitting a box to the view keeps a 1:1 aspect ratio") {
	FixedViewport host(800, 400);   // twice as wide as it is tall
	CanvasCamera cam(&host);

	// A square box: the height is the limiting factor against this viewport.
	cam.setViewport(GLPoint2f(0, 100), GLPoint2f(100, 0));

	GLPoint2f topLeft, bottomRight;
	cam.getViewport(topLeft, bottomRight);

	// The box's height fills the view exactly...
	CHECK(topLeft.y == doctest::Approx(100.0));
	CHECK(bottomRight.y == doctest::Approx(0.0));
	// ...and it is centred horizontally, with equal margins either side.
	CHECK(topLeft.x + bottomRight.x == doctest::Approx(100.0));
	// Square pixels: one world unit is the same size in x and y.
	CHECK((bottomRight.x - topLeft.x) / 800.0
	      == doctest::Approx((topLeft.y - bottomRight.y) / 400.0));
}

TEST_CASE("a camera with no host does not crash") {
	// Constructed but not yet attached to a window, which happens during setup.
	CanvasCamera cam;
	cam.setPan(5.0, 5.0);
	cam.setZoom(0.5);
	GLdouble x, y;
	cam.getPan(x, y);
	CHECK(x == doctest::Approx(5.0));
}
