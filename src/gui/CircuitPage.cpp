/*****************************************************************************
   Project: CEDAR Logic Simulator

   CircuitPage: page contents and how they draw, with no toolkit attached.
*****************************************************************************/

#include "CircuitPage.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "gl_defs.h"
#include "guiGate.h"
#include "guiWire.h"
#include "klsBBox.h"
#include "Settings.h"
#include "render/Scene.h"
#include "render/RenderStyle.h"

void CircuitPage::renderToScene(cl::render::Scene& scene,
                              const cl::render::RenderStyle& style,
                              int deviceW, int deviceH,
                              float horizSpacing, float vertSpacing) {
	using namespace cl::render;

	klsBBox world;
	for (auto it = gateList.begin(); it != gateList.end(); ++it)
		if (it->second) world.addBBox(it->second->getBBox());
	for (auto it = wireList.begin(); it != wireList.end(); ++it)
		if (it->second) world.addBBox(it->second->getBBox());

	float minX, minY, maxX, maxY;
	if (world.empty()) { minX = minY = -50; maxX = maxY = 50; }
	else { minX = world.getLeft(); maxX = world.getRight();
	       minY = world.getBottom(); maxY = world.getTop(); }

	float worldW = std::max(1e-3f, maxX - minX);
	float worldH = std::max(1e-3f, maxY - minY);
	float scale = 0.92f * std::min((float)deviceW / worldW,
	                               (float)deviceH / worldH);
	float offX = ((float)deviceW - worldW * scale) * 0.5f;
	float offY = ((float)deviceH - worldH * scale) * 0.5f;

	// world -> device: x' = (x - minX)*scale + offX ; y' = (maxY - y)*scale + offY
	Transform t;
	t.a = scale;  t.c = 0;      t.e = -minX * scale + offX;
	t.b = 0;      t.d = -scale; t.f =  maxY * scale + offY;

	// Visible world rect = the full device rectangle back-projected, so the grid
	// fills the image the way GL fills the visible viewport.
	const float gMinX = minX - offX / scale;
	const float gMaxX = minX + ((float)deviceW - offX) / scale;
	const float gMinY = maxY - ((float)deviceH - offY) / scale;
	const float gMaxY = maxY + offY / scale;
	drawSceneContents(scene, style, t, scale, horizSpacing, vertSpacing,
	                  gMinX, gMinY, gMaxX, gMaxY);
}

// Render at the LIVE camera (pan/zoom), not the bbox fit -- this is the
// on-screen path. The camera is what the user has panned and zoomed to:
//   world x in [panX, panX + w*zoom], y in [panY - h*zoom, panY], mapped to
// physical pixels. So device px per world unit = contentScale / zoom, and world
// y is flipped for the top-left device origin.
void CircuitPage::renderLiveToScene(cl::render::Scene& scene,
                                    const cl::render::RenderStyle& style,
                                    const CanvasCamera& camera,
                                    float contentScale) {
	using namespace cl::render;
	GLdouble px, py;
	camera.getPan(px, py);
	double vz = camera.getZoom();
	if (vz <= 0) vz = 1.0;

	const int w = camera.viewportWidth();
	const int h = camera.viewportHeight();
	const float scale = (float)(contentScale / vz);

	Transform t;
	t.a = scale;  t.c = 0; t.e = (float)(-px * scale);
	t.b = 0; t.d = -scale; t.f = (float)( py * scale);

	const float gMinX = (float)px;
	const float gMaxX = (float)(px + w * vz);
	const float gMinY = (float)(py - h * vz);
	const float gMaxY = (float)py;

	drawSceneContents(scene, style, t, scale,
	                  camera.horizSpacing(), camera.vertSpacing(),
	                  gMinX, gMinY, gMaxX, gMaxY);
}

// Draw the grid + wires + gates into `scene` under an already-computed viewport
// transform. Shared by the bbox-fit export path (renderToScene) and the live
// camera path (renderLiveToScene). `scale` is device px per world unit; the
// g{Min,Max}{X,Y} bounds are the visible world rectangle for the grid.
// The background grid, matching klsGLCanvas: the base world spacing snaps to an
// integer (>=1), then grows so on-screen lines stay at least
// MIN_GRID_SCREEN_SPACING px apart when zoomed out. Faint translucent blue
// (GRID_INTENSITY as both blue and alpha). Assumes the viewport is already set;
// it is camera-dependent so the live path draws it fresh every frame.
void CircuitPage::drawGridInto(cl::render::Scene& scene,
                             const cl::render::RenderStyle& style, float scale,
                             float horizSpacing, float vertSpacing,
                             float gMinX, float gMinY, float gMaxX, float gMaxY) {
	using namespace cl::render;
	if (!style.showGrid) return;
	const float viewZoom = 1.0f / scale;   // GL viewZoom = world units / pixel
	const long spaceX = std::max(std::max((long)(horizSpacing + 0.5f), 1L),
	                             (long)(MIN_GRID_SCREEN_SPACING * viewZoom));
	const long spaceY = std::max(std::max((long)(vertSpacing + 0.5f), 1L),
	                             (long)(MIN_GRID_SCREEN_SPACING * viewZoom));
	Stroke grid;
	grid.color = Color(0.0f, 0.0f, (float)GRID_INTENSITY, (float)GRID_INTENSITY);
	grid.width = 1.0f;
	std::vector<Point> gl;
	for (long x = (long)std::floor(gMinX / spaceX) * spaceX; x <= gMaxX; x += spaceX) {
		gl.push_back(Point((float)x, gMinY)); gl.push_back(Point((float)x, gMaxY));
	}
	for (long y = (long)std::floor(gMinY / spaceY) * spaceY; y <= gMaxY; y += spaceY) {
		gl.push_back(Point(gMinX, (float)y)); gl.push_back(Point(gMaxX, (float)y));
	}
	if (!gl.empty()) scene.lines(&gl[0], gl.size(), grid);
}

// The circuit itself: wires then gates. Assumes the viewport/matrix is already
// set by the caller (the live path records this into an SkPicture, so it must
// NOT set the viewport here).
void CircuitPage::drawCircuitInto(cl::render::Scene& scene,
                                const cl::render::RenderStyle& style) {
	for (auto it = wireList.begin(); it != wireList.end(); ++it)
		if (it->second) it->second->drawToScene(scene, style);
	for (auto it = gateList.begin(); it != gateList.end(); ++it)
		if (it->second) it->second->drawToScene(scene, style);
}

void CircuitPage::drawSceneContents(cl::render::Scene& scene,
                                  const cl::render::RenderStyle& style,
                                  const cl::render::Transform& t, float scale,
                                  float horizSpacing, float vertSpacing,
                                  float gMinX, float gMinY,
                                  float gMaxX, float gMaxY) {
	scene.setViewport(t);
	drawGridInto(scene, style, scale, horizSpacing, vertSpacing,
	             gMinX, gMinY, gMaxX, gMaxY);
	drawCircuitInto(scene, style);
}

// A cheap signature of everything that affects the rendered circuit: gate
// positions + selection, and wire selection + signal state (which drives every
// state colour and per-type fill). The live path re-records its SkPicture only
// when this changes, so a pure pan (nothing here changes) replays the cache.
unsigned long long CircuitPage::renderContentKey() {
	unsigned long long sig = 1469598103934665603ULL;
	auto mix = [&sig](unsigned long long v) { sig = (sig ^ v) * 1099511628211ULL; };
	// Global settings that change the drawing but aren't per-object state: wire
	// connection dots are gated on wireConnVisible and sized by wireConnRadius.
	mix(appConfig().appSettings.wireConnVisible ? 1u : 0u);
	{ float r = (float)appConfig().appSettings.wireConnRadius; unsigned u;
	  std::memcpy(&u, &r, sizeof u); mix(u); }
	// Fold each gate's/wire's full appearance -- transform, params, selection and
	// signal state -- so any edit (move, rotate, reshape, toggle, param change)
	// invalidates the retained SkPicture. Anything omitted here replays stale.
	for (auto it = gateList.begin(); it != gateList.end(); ++it) {
		if (!it->second) continue;
		mix(it->first);
		mix(it->second->appearanceHash());
	}
	for (auto it = wireList.begin(); it != wireList.end(); ++it) {
		if (!it->second) continue;
		mix(it->first);
		mix(it->second->appearanceHash());
	}
	return sig;
}

