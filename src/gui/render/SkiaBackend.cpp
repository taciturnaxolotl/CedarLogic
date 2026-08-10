// SkiaBackend -- Ganesh GL context + surfaces + G0 probe. See SkiaBackend.h.

#ifdef WITH_SKIA

#include "render/SkiaBackend.h"

#include <cstdlib>
#include <memory>

#include "core/SkCanvas.h"
#include "core/SkRect.h"
#include "svg/SkSVGCanvas.h"
#include "core/SkColor.h"
#include "core/SkColorSpace.h"
#include "core/SkDocument.h"
#include "core/SkFont.h"
#include "core/SkImage.h"
#include "core/SkPicture.h"
#include "core/SkPictureRecorder.h"
#include "core/SkPixmap.h"
#include "core/SkRect.h"
#include "core/SkSamplingOptions.h"
#include "core/SkStream.h"
#include "encode/SkPngEncoder.h"
#include "render/SkiaProbe.h"
#include "render/SkiaScene.h"
#include "core/SkFontMgr.h"
#include "core/SkFontStyle.h"
#include "core/SkImageInfo.h"
#include "core/SkMatrix.h"
#include "core/SkPaint.h"
#include "core/SkPath.h"
#include "core/SkSurface.h"
#include "core/SkSurfaceProps.h"
#include "core/SkTypeface.h"
#include "docs/SkPDFDocument.h"
#include "gpu/GrBackendSurface.h"
#include "gpu/GrDirectContext.h"
#include "gpu/GrTypes.h"
#include "gpu/gl/GrGLInterface.h"
#include "gpu/gl/GrGLTypes.h"
#include "gpu/ganesh/SkSurfaceGanesh.h"
#include "gpu/ganesh/gl/GrGLBackendSurface.h"
#include "gpu/ganesh/gl/GrGLDirectContext.h"
#include "ports/SkFontMgr_directory.h"


namespace cl {
namespace render {
namespace {
// Sized GL internal format for an 8-bit RGBA framebuffer (GL_RGBA8). Declared
// locally to avoid pulling a platform GL header into this TU.
const unsigned int kGLRGBA8 = 0x8058;
}  // namespace

SkiaBackend& SkiaBackend::get() {
	static SkiaBackend instance;
	return instance;
}

bool SkiaBackend::ensureContext() {
	if (fContext) return true;
	fInterface = GrGLMakeNativeInterface();
	fContext = GrDirectContexts::MakeGL(fInterface);
	return fContext != nullptr;
}

void SkiaBackend::resetGLStateForLegacy() {
	if (!fInterface) return;
	// GL enums (avoid pulling a platform GL header). Restore the state the app's
	// fixed-function GL path (the renderer-toggle fallback + shared-context
	// canvases) assumes -- Ganesh both leaves modern-pipeline objects bound AND
	// leaves scissor/stencil/depth/cull enabled and mutates masks/blend; a
	// leftover scissor rect would clip the legacy path's clear and every draw.
	const unsigned GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER = 0x8893;
	const unsigned GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE0 = 0x84C0, GL_FRAMEBUFFER = 0x8D40;
	const unsigned GL_SCISSOR_TEST = 0x0C11, GL_STENCIL_TEST = 0x0B90;
	const unsigned GL_DEPTH_TEST = 0x0B71, GL_CULL_FACE = 0x0B44, GL_BLEND = 0x0BE2;
	const unsigned GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303;
	const GrGLInterface::Functions& gl = fInterface->fFunctions;
	if (gl.fUseProgram)      gl.fUseProgram(0);
	if (gl.fBindVertexArray) gl.fBindVertexArray(0);
	if (gl.fBindBuffer)    { gl.fBindBuffer(GL_ARRAY_BUFFER, 0);
	                         gl.fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
	if (gl.fActiveTexture)   gl.fActiveTexture(GL_TEXTURE0);
	if (gl.fBindTexture)     gl.fBindTexture(GL_TEXTURE_2D, 0);
	if (gl.fBindFramebuffer) gl.fBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (gl.fDisable)       { gl.fDisable(GL_SCISSOR_TEST); gl.fDisable(GL_STENCIL_TEST);
	                         gl.fDisable(GL_DEPTH_TEST);   gl.fDisable(GL_CULL_FACE); }
	if (gl.fDepthMask)       gl.fDepthMask(1);
	if (gl.fColorMask)       gl.fColorMask(1, 1, 1, 1);
	// The app renders with alpha blending on (e.g. the translucent grid).
	if (gl.fEnable)          gl.fEnable(GL_BLEND);
	if (gl.fBlendFunc)       gl.fBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

sk_sp<SkSurface> SkiaBackend::windowSurface(int width, int height, int fboId,
                                            int sampleCount, int stencilBits) {
	if (!ensureContext()) return nullptr;

	GrGLFramebufferInfo info;
	info.fFBOID = static_cast<GrGLuint>(fboId);
	info.fFormat = kGLRGBA8;

	GrBackendRenderTarget target = GrBackendRenderTargets::MakeGL(
		width, height, sampleCount, stencilBits, info);

	SkSurfaceProps props;
	return SkSurfaces::WrapBackendRenderTarget(
		fContext.get(), target, kBottomLeft_GrSurfaceOrigin,
		kRGBA_8888_SkColorType, nullptr, &props);
}

sk_sp<SkSurface> SkiaBackend::rasterSurface(int width, int height) {
	return SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
}

const SkFont* SkiaBackend::defaultFont() {
	if (fFontTried) return fFont;
	fFontTried = true;

	// This Skia is built with the custom-directory font manager only (no platform
	// system manager), so a face has to come from a file on disk. Resolve one at
	// runtime, most-specific first, so text works with no configuration:
	//   1. CEDAR_FONT_FILE   -- explicit override (a single .ttf)
	//   2. res/LabelFont.ttf -- a face bundled next to the app, if we ship one
	//   3. a platform system sans-serif at its well-known path
	// The platform list ends on the first hit; on Windows that is real Arial,
	// which matches the GL renderer's baked arial.glf atlas.
	//
	// makeFromFile takes a full path; the "." directory argument only scopes
	// family matching, which we don't use here.
	fFontMgr = SkFontMgr_New_Custom_Directory(".");
	if (!fFontMgr) return fFont;

	// Bold faces first: the GL renderer's baked arial.glf atlas is bold, so
	// schematic labels read heavier than a book weight. Matching it keeps Skia
	// output visually identical to the legacy renderer. Regular is the fallback.
	const char* env = std::getenv("CEDAR_FONT_FILE");
	const char* candidates[] = {
		env,
		"res/LabelFont.ttf",
		"LabelFont.ttf",
#if defined(_WIN32)
		"C:/Windows/Fonts/arialbd.ttf",
		"C:/Windows/Fonts/arial.ttf",
		"C:/Windows/Fonts/segoeui.ttf",
#elif defined(__APPLE__)
		"/Library/Fonts/Arial Bold.ttf",
		"/System/Library/Fonts/Helvetica.ttc",
		"/Library/Fonts/Arial.ttf",
#else
		"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
		"/usr/share/fonts/dejavu/DejaVuSans.ttf",
#endif
	};
	for (const char* path : candidates) {
		if (!path || !*path) continue;
		fTypeface = fFontMgr->makeFromFile(path, 0);
		if (fTypeface) break;
	}

	// Last resort: a directory of faces via CEDAR_FONT_DIR.
	if (!fTypeface) {
		const char* dir = std::getenv("CEDAR_FONT_DIR");
		if (dir && *dir) {
			sk_sp<SkFontMgr> dm = SkFontMgr_New_Custom_Directory(dir);
			if (dm) {
				fTypeface = dm->matchFamilyStyle(nullptr, SkFontStyle::Normal());
				if (!fTypeface) {
					fTypeface = dm->legacyMakeTypeface(nullptr,
					                                   SkFontStyle::Normal());
				}
			}
		}
	}

	if (fTypeface) fFont = new SkFont(fTypeface, 12.0f);
	return fFont;
}

void SkiaBackend::probe(SkSurface* surface) {
	if (!surface) return;
	SkCanvas* canvas = surface->getCanvas();
	canvas->clear(SK_ColorWHITE);

	SkPaint stroke;
	stroke.setAntiAlias(true);
	stroke.setStyle(SkPaint::kStroke_Style);
	stroke.setStrokeWidth(3.0f);
	stroke.setColor(SK_ColorBLACK);

	SkPath path;
	path.moveTo(20, 20);
	path.lineTo(120, 60);
	path.quadTo(160, 20, 200, 80);
	canvas->drawPath(path, stroke);

	const SkFont* font = defaultFont();
	if (font) {
		SkPaint text;
		text.setAntiAlias(true);
		text.setColor(SK_ColorBLACK);
		canvas->drawString("CedarLogic + Skia", 20, 140, *font, text);
	}

	if (fContext) fContext->flushAndSubmit();
}

bool skiaProbeToPng(const char* path, int width, int height) {
	SkiaBackend& backend = SkiaBackend::get();
	sk_sp<SkSurface> surface = backend.rasterSurface(width, height);
	if (!surface) return false;
	backend.probe(surface.get());  // draws the path + text (no GPU flush needed)

	sk_sp<SkImage> image = surface->makeImageSnapshot();
	if (!image) return false;
	SkPixmap pixmap;
	if (!image->peekPixels(&pixmap)) return false;

	SkFILEWStream out(path);
	if (!out.isValid()) return false;
	return SkPngEncoder::Encode(&out, pixmap, SkPngEncoder::Options{});
}

bool skiaRenderToPng(const char* path, int width, int height,
                     const std::function<void(Scene&)>& draw) {
	sk_sp<SkSurface> surface = SkiaBackend::get().rasterSurface(width, height);
	if (!surface) return false;
	surface->getCanvas()->clear(SK_ColorWHITE);

	SkiaScene scene(surface->getCanvas(), SkiaBackend::get().defaultFont());
	draw(scene);

	sk_sp<SkImage> image = surface->makeImageSnapshot();
	if (!image) return false;
	SkPixmap pixmap;
	if (!image->peekPixels(&pixmap)) return false;
	SkFILEWStream out(path);
	if (!out.isValid()) return false;
	return SkPngEncoder::Encode(&out, pixmap, SkPngEncoder::Options{});
}

bool skiaRenderWindow(int width, int height, int fboId,
                      const std::function<void(Scene&)>& draw, float strokeScale) {
	SkiaBackend& backend = SkiaBackend::get();
	sk_sp<SkSurface> surface = backend.windowSurface(width, height, fboId);
	if (!surface) return false;
	GrDirectContext* ctx = backend.context();
	// The GL context is shared with wx and (until they migrate) other GL canvases,
	// so its state changes behind Skia's back between frames. Invalidate Skia's
	// cached GL state each frame or stale bindings corrupt the render (flashing,
	// stretched glyphs from a wrong texture/transform).
	if (ctx) ctx->resetContext();
	SkCanvas* canvas = surface->getCanvas();
	canvas->clear(SK_ColorWHITE);
	SkiaScene scene(canvas, backend.defaultFont(), strokeScale);
	draw(scene);
	// Flush the recorded work to GL and hand back to the caller to SwapBuffers.
	if (ctx) ctx->flushAndSubmit();
	backend.resetGLStateForLegacy();   // leave the shared GL context fixed-function-ready
	return true;
}

bool SkiaBackend::minimapCacheHit(unsigned long long key, int w, int h) const {
	return fMinimapCache && fMinimapKey == key && fMinimapW == w && fMinimapH == h;
}

SkImage* SkiaBackend::minimapCacheImage() const { return fMinimapCache.get(); }

void SkiaBackend::setMinimapCache(sk_sp<SkImage> img, unsigned long long key,
                                  int w, int h) {
	fMinimapCache = img;
	fMinimapKey = key;
	fMinimapW = w;
	fMinimapH = h;
}

bool SkiaBackend::sceneCacheHit(unsigned long long key) const {
	return fScenePicture && fSceneKey == key;
}

SkPicture* SkiaBackend::sceneCachePicture() const { return fScenePicture.get(); }

void SkiaBackend::setSceneCache(sk_sp<SkPicture> pic, unsigned long long key) {
	fScenePicture = pic;
	fSceneKey = key;
}

bool skiaRenderWindowScene(int width, int height, int fboId,
                           unsigned long long sceneKey,
                           const Transform& camera,
                           const std::function<void(Scene&)>& drawGrid,
                           const std::function<void(Scene&)>& drawScene) {
	SkiaBackend& backend = SkiaBackend::get();
	sk_sp<SkSurface> surface = backend.windowSurface(width, height, fboId);
	if (!surface) return false;
	GrDirectContext* ctx = backend.context();
	if (ctx) ctx->resetContext();
	SkCanvas* canvas = surface->getCanvas();
	canvas->clear(SK_ColorWHITE);

	// Grid: camera-dependent (fills the viewport), so drawn live every frame.
	{
		SkiaScene grid(canvas, backend.defaultFont());
		drawGrid(grid);
	}

	// Circuit: recorded once in world coords and reused until the content or the
	// camera SCALE changes. Key on the scale so device-pixel stroke widths, which
	// bake into the picture, stay correct; a pure pan keeps the scale and just
	// replays under a new translation.
	unsigned long long key = (sceneKey * 1099511628211ULL) ^
	                         (unsigned long long)(camera.a * 4096.0f);
	if (!backend.sceneCacheHit(key)) {
		SkPictureRecorder rec;
		SkCanvas* pc = rec.beginRecording(SkRect::MakeLTRB(-1e6f, -1e6f, 1e6f, 1e6f));
		// Bake the camera's scale (and y-flip) into the picture; translation is
		// applied at replay, so panning doesn't invalidate it.
		pc->scale(camera.a, camera.d);
		SkiaScene scene(pc, backend.defaultFont());
		drawScene(scene);   // wires + gates; must not setViewport
		backend.setSceneCache(rec.finishRecordingAsPicture(), key);
	}
	// The grid left the canvas matrix at the full camera; replay the picture (which
	// already bakes the scale) under just the translation, so the net is the full
	// camera again.
	canvas->save();
	canvas->setMatrix(SkMatrix::Translate(camera.e, camera.f));
	canvas->drawPicture(backend.sceneCachePicture());
	canvas->restore();

	if (ctx) ctx->flushAndSubmit();
	backend.resetGLStateForLegacy();   // leave the shared GL context fixed-function-ready
	return true;
}

bool skiaRenderWindowCached(int width, int height, int fboId,
                            unsigned long long contentKey,
                            const std::function<void(Scene&)>& drawStatic,
                            const std::function<void(Scene&)>& drawOverlay,
                            float strokeScale) {
	SkiaBackend& backend = SkiaBackend::get();
	sk_sp<SkSurface> surface = backend.windowSurface(width, height, fboId);
	if (!surface) return false;
	GrDirectContext* ctx = backend.context();
	if (ctx) ctx->resetContext();
	SkCanvas* canvas = surface->getCanvas();
	canvas->clear(SK_ColorWHITE);

	if (backend.minimapCacheHit(contentKey, width, height)) {
		// Re-blit the cached circuit thumbnail (device space) -- no redraw.
		canvas->drawImage(backend.minimapCacheImage(), 0, 0,
		                  SkSamplingOptions{}, nullptr);
	} else {
		SkiaScene scene(canvas, backend.defaultFont(), strokeScale);
		drawStatic(scene);
		// Snapshot the circuit layer (before the overlay) for reuse on pan.
		backend.setMinimapCache(surface->makeImageSnapshot(), contentKey,
		                        width, height);
	}
	// The moving overlay (viewport rectangle) is always redrawn on top.
	SkiaScene overlay(canvas, backend.defaultFont(), strokeScale);
	drawOverlay(overlay);
	if (ctx) ctx->flushAndSubmit();
	backend.resetGLStateForLegacy();   // leave the shared GL context fixed-function-ready
	return true;
}

bool skiaRenderToSvg(const char* path, int width, int height,
                     const std::function<void(Scene&)>& draw) {
	SkFILEWStream out(path);
	if (!out.isValid()) return false;
	{
		// SkSVGCanvas streams the SVG as it draws and finalizes when the canvas
		// is destroyed, so scope it tightly around the draw.
		SkRect bounds = SkRect::MakeIWH(width, height);
		std::unique_ptr<SkCanvas> canvas = SkSVGCanvas::Make(bounds, &out);
		if (!canvas) return false;
		canvas->drawColor(SK_ColorWHITE);   // GL export ground is white
		SkiaScene scene(canvas.get(), SkiaBackend::get().defaultFont());
		draw(scene);
	}
	return true;
}

bool skiaRenderToPdf(const char* path, int width, int height,
                     const std::function<void(Scene&)>& draw) {
	SkFILEWStream out(path);
	if (!out.isValid()) return false;

	SkPDF::Metadata meta;
	meta.fTitle = "CedarLogic circuit";
	meta.fCreator = "CedarLogic";
	sk_sp<SkDocument> doc = SkPDF::MakeDocument(&out, meta);
	if (!doc) return false;

	SkCanvas* canvas = doc->beginPage((float)width, (float)height);
	canvas->clear(SK_ColorWHITE);
	SkiaScene scene(canvas, SkiaBackend::get().defaultFont());
	draw(scene);
	doc->endPage();
	doc->close();
	return true;
}

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
