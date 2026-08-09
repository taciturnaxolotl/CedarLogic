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
#include "core/SkPixmap.h"
#include "core/SkStream.h"
#include "encode/SkPngEncoder.h"
#include "render/SkiaProbe.h"
#include "render/SkiaScene.h"
#include "core/SkFontMgr.h"
#include "core/SkFontStyle.h"
#include "core/SkImageInfo.h"
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
	sk_sp<const GrGLInterface> iface = GrGLMakeNativeInterface();
	fContext = GrDirectContexts::MakeGL(iface);
	return fContext != nullptr;
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
                      const std::function<void(Scene&)>& draw) {
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
	SkiaScene scene(canvas, backend.defaultFont());
	draw(scene);
	// Flush the recorded work to GL and hand back to the caller to SwapBuffers.
	if (ctx) ctx->flushAndSubmit();
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
