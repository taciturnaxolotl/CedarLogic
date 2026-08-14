// SkiaBackend -- Ganesh GL context + surfaces + G0 probe. See SkiaBackend.h.

#ifdef WITH_SKIA

#include "render/SkiaBackend.h"

#include <cstdlib>
#include <memory>
#include <string>

#include "core/SkCanvas.h"
#include "core/SkRect.h"
#include "svg/SkSVGCanvas.h"
#include "core/SkColor.h"
#include "core/SkColorSpace.h"
#include "core/SkDocument.h"
#include "core/SkFont.h"
#include "core/SkFontTypes.h"
#include <cstring>
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
#include "core/SkFontMetrics.h"
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
#include "ports/SkFontMgr_empty.h"
#ifdef CEDAR_SKIA_FONTCONFIG
#include "ports/SkFontMgr_fontconfig.h"
#ifdef CEDAR_SKIA_FONTCONFIG_SCANNER
#include "ports/SkFontScanner_FreeType.h"
#endif
#endif


namespace cl {
namespace render {
namespace {
// Sized GL internal format for an 8-bit RGBA framebuffer (GL_RGBA8). Declared
// locally to avoid pulling a platform GL header into this TU.
const unsigned int kGLRGBA8 = 0x8058;

// Where a bundled face may be found, set once at startup (see setFontSearchDir).
// Empty until then, and empty in headless tools that never call it.
std::string gFontSearchDir;
}  // namespace

void setFontSearchDir(const char* dir) {
	gFontSearchDir = (dir && *dir) ? dir : "";
}

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

	// Resolve a face at runtime, most-specific first, so text works with no
	// configuration:
	//   1. CEDAR_FONT_FILE     -- explicit override (a single .ttf)
	//   2. <resources>/res/LabelFont.ttf -- a face bundled with the app, if we
	//                             ship one (setFontSearchDir supplies the root)
	//   3. a platform system sans-serif at its well-known path
	//   4. fontconfig, where the build has it -- the only reliable answer on
	//      Linux, whose font layout differs per distro and is absent on NixOS
	//   5. CEDAR_FONT_DIR      -- a directory of faces, the last resort
	// The platform list ends on the first hit; on Windows that is real Arial,
	// which matches the GL renderer's baked arial.glf atlas.
	//
	// An EMPTY custom manager, not a directory one: makeFromFile takes a full
	// path and needs no preloaded families, while SkFontMgr_New_Custom_Directory
	// eagerly walks its directory tree (every file, every depth) in the
	// constructor. Handing it "." froze the first paint for as long as the
	// working directory was large.
	fFontMgr = SkFontMgr_New_Custom_Empty();
	if (!fFontMgr) return fFont;

	// Bold faces first: the GL renderer's baked arial.glf atlas is bold, so
	// schematic labels read heavier than a book weight. Matching it keeps Skia
	// output visually identical to the legacy renderer. Regular is the fallback.
	const char* env = std::getenv("CEDAR_FONT_FILE");
	// The bundled face lives under the app's resources dir, which is nowhere near
	// the working directory once the app is launched from a menu entry.
	const std::string bundled = gFontSearchDir.empty()
		? std::string() : gFontSearchDir + "res/LabelFont.ttf";
	const char* candidates[] = {
		env,
		bundled.empty() ? nullptr : bundled.c_str(),
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

#ifdef CEDAR_SKIA_FONTCONFIG
	// Ask fontconfig for the system sans-serif. Hardcoded paths only ever covered
	// Debian and Ubuntu; fontconfig is how every Linux answers this question, and
	// it is the only answer on distros that have no /usr/share/fonts at all.
	if (!fTypeface) {
#ifdef CEDAR_SKIA_FONTCONFIG_SCANNER
		sk_sp<SkFontMgr> fc = SkFontMgr_New_FontConfig(
			nullptr, SkFontScanner_Make_FreeType());   // post-m124 signature
#else
		sk_sp<SkFontMgr> fc = SkFontMgr_New_FontConfig(nullptr);
#endif
		if (fc) {
			// legacyMakeTypeface first: with a null family this is the request
			// fontconfig actually resolves ("whatever the system defaults to"),
			// where matchFamilyStyle wants a real family name.
			fTypeface = fc->legacyMakeTypeface(nullptr, SkFontStyle::Bold());
			if (!fTypeface) {
				fTypeface = fc->matchFamilyStyle("sans-serif", SkFontStyle::Bold());
			}
			if (fTypeface) fFontMgr = fc;
		}
	}
#endif

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

bool skiaRenderToRGB(int width, int height,
                     const std::function<void(Scene&)>& draw,
                     unsigned char* outRgb) {
	if (width <= 0 || height <= 0 || !outRgb) return false;
	sk_sp<SkSurface> surface = SkiaBackend::get().rasterSurface(width, height);
	if (!surface) return false;
	surface->getCanvas()->clear(SK_ColorWHITE);

	SkiaScene scene(surface->getCanvas(), SkiaBackend::get().defaultFont());
	draw(scene);

	// Read back straight into a tightly packed RGB buffer: wxImage owns its data
	// as 3-byte RGB, so let Skia do the N32 -> RGB conversion during readPixels
	// rather than unpacking by hand.
	SkImageInfo info = SkImageInfo::Make(width, height, kRGB_888x_SkColorType,
	                                     kUnpremul_SkAlphaType);
	std::vector<unsigned char> tmp((size_t)width * height * 4);
	if (!surface->readPixels(info, tmp.data(), (size_t)width * 4, 0, 0)) return false;
	for (size_t i = 0, n = (size_t)width * height; i < n; i++) {
		outRgb[i * 3 + 0] = tmp[i * 4 + 0];
		outRgb[i * 3 + 1] = tmp[i * 4 + 1];
		outRgb[i * 3 + 2] = tmp[i * 4 + 2];
	}
	return true;
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
                           const std::function<void(Scene&)>& drawScene,
                           const std::function<void(Scene&)>& drawOverlay) {
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

	// Interactive overlays, drawn live on top of the replayed picture (not cached,
	// not part of sceneKey) so a mouse move doesn't invalidate the circuit picture.
	if (drawOverlay) {
		SkiaScene overlay(canvas, backend.defaultFont());
		drawOverlay(overlay);
	}

	if (ctx) ctx->flushAndSubmit();
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

float measuredTextWidth(const char* utf8, float pixelHeight) {
	if (!utf8 || !*utf8) return 0.0f;
	const SkFont* base = SkiaBackend::get().defaultFont();
	if (!base) return 0.0f;
	// Measure exactly the way SkiaScene::text draws: at kGlyphUnits, scaled down.
	SkFont f(*base);
	f.setSize(kGlyphUnits);
	f.setHinting(SkFontHinting::kNone);
	f.setLinearMetrics(true);
	const float w = (float)f.measureText(utf8, std::strlen(utf8),
	                                     SkTextEncoding::kUTF8);
	return w * (pixelHeight / kGlyphUnits);
}

float measuredTextHeight(float pixelHeight) {
	const SkFont* base = SkiaBackend::get().defaultFont();
	if (!base) return 0.0f;
	SkFont f(*base);
	f.setSize(kGlyphUnits);
	f.setHinting(SkFontHinting::kNone);
	f.setLinearMetrics(true);
	SkFontMetrics fm;
	f.getMetrics(&fm);
	// What SkiaScene::text actually covers: it hangs the text from the top of
	// the capitals, so the drawn box runs from there down past the descenders.
	const float capHeight = fm.fCapHeight > 0.0f ? fm.fCapHeight : -fm.fAscent;
	return (capHeight + fm.fDescent) * (pixelHeight / kGlyphUnits);
}

}  // namespace render
}  // namespace cl

#endif  // WITH_SKIA
