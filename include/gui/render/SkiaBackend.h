// SkiaBackend -- binds Skia's Ganesh GL backend to one of CedarLogic's GL
// contexts (Workstream G, phase G0).
//
// One instance per GL context, never fewer: a GrDirectContext is bound to the
// GL context it was built on. Every GL canvas owns a context and one of these
// through CanvasGL.
//
// Font state is the exception and stays process-wide: a typeface is CPU-side,
// so the static accessors below are shared by every backend and by the
// offscreen paths that have no GL context at all.
//
// Compiles only under WITH_SKIA.

#ifndef CL_RENDER_SKIABACKEND_H
#define CL_RENDER_SKIABACKEND_H

#ifdef WITH_SKIA

#include <cstddef>

#include "core/SkRefCnt.h"

class GrDirectContext;
struct GrGLInterface;
class SkSurface;
class SkImage;
class SkPicture;
class SkFont;

namespace cl {
namespace render {

// How much GPU memory one backend's Ganesh cache may hold. Skia's own default is
// ~96MB, affordable with a single context and not now that every canvas has one:
// tabs are user-addable, so the count is theirs to choose.
const size_t kSkiaCacheBudgetMain = 64u * 1024u * 1024u;
const size_t kSkiaCacheBudgetSmall = 12u * 1024u * 1024u;

class SkiaBackend {
public:
	// Construct one per GL context; cheap and allocation-free until the first
	// ensureContext(). `cacheBudgetBytes` caps the Ganesh resource cache.
	explicit SkiaBackend(size_t cacheBudgetBytes = kSkiaCacheBudgetMain);
	~SkiaBackend();

	// Build the GrDirectContext from the currently-current GL context
	// (idempotent). Returns false if Skia could not adopt it. The GL context this
	// first succeeds under is the one the backend is bound to from then on.
	bool ensureContext();

	GrDirectContext* context() { return fContext.get(); }

	// Wrap the currently-bound GL framebuffer (fboId, usually 0 for the window)
	// as a render target surface. Caller must have made the GL context current.
	sk_sp<SkSurface> windowSurface(int width, int height, int fboId = 0,
	                               int sampleCount = 0, int stencilBits = 8);

	// A CPU raster surface for offscreen rendering (PNG/SVG/PDF, goldens). Needs
	// no GL context, hence static.
	static sk_sp<SkSurface> rasterSurface(int width, int height);

	// A default typeface for text, or null if none is available. Resolved once
	// per process, most-specific first: CEDAR_FONT_FILE, a bundled res/ face,
	// then a platform system sans-serif. See the definition for the full order.
	static const SkFont* defaultFont();

	// G0 proof: draw one stroked path and (if a font is available) one line of
	// text into `surface`, then flush. Renders to whatever the surface targets.
	void probe(SkSurface* surface);

	// Give up the Ganesh context without touching GL, for the one case where that
	// is the only safe move: the canvas's GL context is already gone, so the GL
	// objects Ganesh would delete have no context to be deleted against. They die
	// with the context anyway.
	void abandon();

	// Minimap thumbnail cache (used by skiaRenderWindowCached). The cached image
	// is reused while `key` and the size match; setMinimapCache replaces it.
	// GPU-backed, so it belongs to this backend's context and no other.
	bool minimapCacheHit(unsigned long long key, int w, int h) const;
	SkImage* minimapCacheImage() const;
	void setMinimapCache(sk_sp<SkImage> img, unsigned long long key, int w, int h);

	// Main-canvas scene picture cache (used by skiaRenderWindowScene). The
	// recorded circuit is reused while `key` matches; a pan is a picture replay,
	// only a content/scale change re-records. Per-backend means per-canvas, so
	// switching tabs no longer evicts the other tab's circuit.
	bool sceneCacheHit(unsigned long long key) const;
	SkPicture* sceneCachePicture() const;
	void setSceneCache(sk_sp<SkPicture> pic, unsigned long long key);

private:
	SkiaBackend(const SkiaBackend&);
	SkiaBackend& operator=(const SkiaBackend&);

	size_t fCacheBudget;
	sk_sp<GrDirectContext> fContext;
	sk_sp<const GrGLInterface> fInterface;   // native GL binding Ganesh drives
	sk_sp<SkImage> fMinimapCache;   // declared after fContext: destructs first
	unsigned long long fMinimapKey = 0;
	int fMinimapW = 0, fMinimapH = 0;
	sk_sp<SkPicture> fScenePicture;   // main-canvas circuit, world coords
	unsigned long long fSceneKey = 0;
};

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
#endif  // CL_RENDER_SKIABACKEND_H
