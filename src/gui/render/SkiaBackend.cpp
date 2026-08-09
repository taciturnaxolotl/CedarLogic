// SkiaBackend -- Ganesh GL context + surfaces + G0 probe. See SkiaBackend.h.

#ifdef WITH_SKIA

#include "render/SkiaBackend.h"

#include <cstdlib>

#include "core/SkCanvas.h"
#include "core/SkColor.h"
#include "core/SkColorSpace.h"
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

	// The bundled face is a G2 decision; for now load from CEDAR_FONT_DIR when
	// set (also how the golden harness will point at a pinned face).
	const char* dir = std::getenv("CEDAR_FONT_DIR");
	if (dir) {
		fFontMgr = SkFontMgr_New_Custom_Directory(dir);
		if (fFontMgr) {
			fTypeface = fFontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
			if (!fTypeface) {
				fTypeface = fFontMgr->legacyMakeTypeface(nullptr,
				                                         SkFontStyle::Normal());
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

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
