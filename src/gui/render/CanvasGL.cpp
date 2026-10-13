// CanvasGL -- see CanvasGL.h for why each canvas owns its own GL context.

#include "render/CanvasGL.h"

#include "wx/dcclient.h"

#include "render/RendererHealth.h"
#ifdef WITH_SKIA
#include "render/SkiaBackend.h"
#endif

namespace cl {
namespace render {
namespace {

// Make `ctx` current without re-pointing its drawable. On macOS that is a real
// distinction, and the whole reason this class exists; elsewhere SetCurrent is
// already nothing more than a make-current call.
bool makeCurrentOnly(wxGLContext* ctx, wxGLCanvas* canvas) {
#ifdef __WXOSX__
	(void)canvas;
	WXGLSetCurrentContext(ctx->GetWXGLContext());
	return true;
#else
	return ctx->SetCurrent(*canvas);
#endif
}

#ifdef __WXMSW__
// Swap interval 0, so a swap does not block the GUI thread until the next
// vblank. This app repaints on demand rather than running a frame loop, so
// there is nothing to gain from pacing to the display. Per context, not per
// process: wglSwapIntervalEXT sets state on whichever context is current.
void disableVSync() {
	typedef BOOL(WINAPI * SwapIntervalProc)(int);
	SwapIntervalProc setSwap =
		(SwapIntervalProc)wglGetProcAddress("wglSwapIntervalEXT");
	if (setSwap) setSwap(0);
}
#endif

}  // namespace

CanvasGL::CanvasGL(wxGLCanvas* canvas, bool smallCache)
		: fCanvas(canvas),
		  fContext(nullptr),
		  fAttachedSize(0, 0),
		  fAttachedScale(0.0),
		  fContextFailed(false)
#ifdef WITH_SKIA
		  , fSkia(new SkiaBackend(smallCache ? kSkiaCacheBudgetSmall
		                                     : kSkiaCacheBudgetMain))
#endif
{
}

CanvasGL::~CanvasGL() {
#ifdef WITH_SKIA
	// Ganesh deletes GL objects as it goes, so its context has to be current for
	// that to land anywhere. Current without re-attaching: re-pointing a view
	// that is already being torn down is the hazard this class exists to avoid.
	// If there is no context the objects were never made, or die with it.
	if (!fContext || !makeCurrentOnly(fContext, fCanvas)) fSkia->abandon();
	delete fSkia;
	fSkia = nullptr;
#endif
	delete fContext;
	fContext = nullptr;
}

bool CanvasGL::ensureContext() {
	if (fContext) return true;
	if (fContextFailed) return false;

#ifdef __WXOSX__
	// macOS hands out a legacy 2.1 context unless asked otherwise, and there the
	// shading language stops at GLSL 1.10 -- old enough that Skia's atlas path
	// renderer emits gl_VertexID into a #version 110 shader, which fails to
	// compile on every path-heavy frame before Skia falls back. 3.2 core is the
	// newest profile macOS offers and the one Ganesh expects. Windows and Linux
	// already hand out a modern compatibility context, so they keep the driver
	// default.
	//
	// Except when simulating an old machine: the processor fallback draws with
	// the fixed pipeline, which a core profile removes, so a forced failure has
	// to take the legacy context too.
	if (forceGLFailure()) {
		fContext = new wxGLContext(fCanvas);
	} else {
		wxGLContextAttrs ctxAttrs;
		ctxAttrs.CoreProfile().EndList();
		fContext = new wxGLContext(fCanvas, NULL, &ctxAttrs);
	}
#else
	fContext = new wxGLContext(fCanvas);
#endif

	// No share group on purpose: each canvas draws its own scene through its own
	// Ganesh context, so there is nothing to share.
	if (fContext && !fContext->IsOK()) {
		delete fContext;
		fContext = nullptr;
	}
	if (!fContext) {
		fContextFailed = true;
		noteRendererFailure("the canvas could not create a GL context");
		return false;
	}
	return true;
}

bool CanvasGL::makeCurrent() {
	if (!ensureContext()) return false;

	const wxSize size = fCanvas->GetClientSize();
	const double scale = fCanvas->GetContentScaleFactor();
	if (size == fAttachedSize && scale == fAttachedScale)
		return makeCurrentOnly(fContext, fCanvas);

	// Shape changed, so the drawable has to be re-pointed at the canvas. This is
	// the only call that does, and it runs on a resize rather than on a frame.
	if (!fContext->SetCurrent(*fCanvas)) return false;
	fAttachedSize = size;
	fAttachedScale = scale;
#ifdef __WXMSW__
	disableVSync();   // needs a current context, which it now is
#endif
	return true;
}

void CanvasGL::paint(const std::function<bool()>& draw) {
	wxPaintDC dc(fCanvas);   // wx requires one in every paint handler, used or not
	if (!makeCurrent()) return;
	// Once a context is current, record what the driver calls itself. Only the
	// first answer sticks, so this is free after the first frame.
	noteGLImplementation((const char*)glGetString(GL_VENDOR),
	                     (const char*)glGetString(GL_RENDERER),
	                     (const char*)glGetString(GL_VERSION));
	if (!draw()) {
		// No frame from the driver or the processor fallback. An untouched back
		// buffer swaps in as black, which reads as a broken app; white does not.
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glFlush();
	fCanvas->SwapBuffers();
}

#ifdef WITH_SKIA
SkiaBackend& CanvasGL::skia() {
	return *fSkia;
}
#endif

}  // namespace render
}  // namespace cl
