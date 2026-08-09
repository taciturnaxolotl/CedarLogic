// SkiaBackend -- binds Skia's Ganesh GL backend to CedarLogic's existing GL
// context (Workstream G, phase G0).
//
// klsGLCanvas is already a wxGLCanvas with a live wxGLContext, so Skia renders
// straight into it: build a GrDirectContext from the current GL context, wrap
// the bound framebuffer as an SkSurface, draw, and let the existing SwapBuffers
// present. A raster surface is offered for the offscreen --render path and
// headless CI. This is the go/no-go: if the context builds and a surface comes
// up on all three platforms, the engine bet is validated.
//
// Compiles only under WITH_SKIA.

#ifndef CL_RENDER_SKIABACKEND_H
#define CL_RENDER_SKIABACKEND_H

#ifdef WITH_SKIA

#include "core/SkRefCnt.h"

class GrDirectContext;
class SkSurface;
class SkFont;
class SkFontMgr;
class SkTypeface;

namespace cl {
namespace render {

class SkiaBackend {
public:
	// One backend per GL context. Call after the GL context is current.
	static SkiaBackend& get();

	// Build the GrDirectContext from the current GL context (idempotent).
	// Returns false if Skia could not adopt the GL context.
	bool ensureContext();

	GrDirectContext* context() { return fContext.get(); }

	// Wrap the currently-bound GL framebuffer (fboId, usually 0 for the window)
	// as a render target surface. Caller must have made the GL context current.
	sk_sp<SkSurface> windowSurface(int width, int height, int fboId = 0,
	                               int sampleCount = 0, int stencilBits = 8);

	// A CPU raster surface for offscreen rendering (PNG/SVG/PDF, goldens).
	sk_sp<SkSurface> rasterSurface(int width, int height);

	// A default typeface for text, or null if none is available. Resolved at
	// runtime, most-specific first: CEDAR_FONT_FILE, a bundled res/ face, then a
	// platform system sans-serif. See defaultFont() for the full search order.
	const SkFont* defaultFont();

	// G0 proof: draw one stroked path and (if a font is available) one line of
	// text into `surface`, then flush. Renders to whatever the surface targets.
	void probe(SkSurface* surface);

private:
	SkiaBackend() {}
	SkiaBackend(const SkiaBackend&);
	SkiaBackend& operator=(const SkiaBackend&);

	sk_sp<GrDirectContext> fContext;
	sk_sp<SkFontMgr> fFontMgr;
	sk_sp<SkTypeface> fTypeface;
	SkFont* fFont = nullptr;
	bool fFontTried = false;
};

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
#endif  // CL_RENDER_SKIABACKEND_H
