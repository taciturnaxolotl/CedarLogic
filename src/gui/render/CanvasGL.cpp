// CanvasGL -- see CanvasGL.h.

#include "render/CanvasGL.h"

#include <cstdio>

#include "wx/dcclient.h"

#include "CrashReporter.h"
#include "render/RendererHealth.h"
#ifdef WITH_SKIA
#include "render/SkiaBackend.h"
#endif

namespace cl {
namespace render {
namespace {

// Make current without re-pointing the drawable, which SetCurrent does on macOS.
bool makeCurrentOnly(wxGLContext* ctx, wxGLCanvas* canvas) {
#ifdef __WXOSX__
	(void)canvas;
	WXGLSetCurrentContext(ctx->GetWXGLContext());
	return true;
#else
	return ctx->SetCurrent(*canvas);
#endif
}

#ifdef __WXOSX__
// AppKit may hand back an unbacked surface inside the resize transaction.
constexpr bool kSkipPresentOnReshape = true;
#else
constexpr bool kSkipPresentOnReshape = false;
#endif

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
		  fContextFailed(false)
#ifdef WITH_SKIA
		  , fSkia(new SkiaBackend(smallCache ? kSkiaCacheBudgetSmall
		                                     : kSkiaCacheBudgetMain))
#endif
{
}

CanvasGL::~CanvasGL() {
#ifdef WITH_SKIA
	// Ganesh frees GL objects as it goes, so make current (without re-attaching).
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

CanvasGL::Current CanvasGL::makeCurrent() {
	if (!ensureContext()) return Current::Failed;

	const Shape now{fCanvas->GetClientSize(), fCanvas->GetContentScaleFactor()};
	if (now == fAttached)
		return makeCurrentOnly(fContext, fCanvas) ? Current::Same : Current::Failed;

	if (!fContext->SetCurrent(*fCanvas)) return Current::Failed;
	fAttached = now;
#ifdef __WXMSW__
	disableVSync();   // needs a current context, which it now is
#endif
	return Current::Reshaped;
}

// The canvas owns this object and wx drops a handler's pending calls when it is
// destroyed, so capturing `this` in CallAfter cannot outlive it.
void CanvasGL::blockPresent() {
	if (fGate != Gate::Blocked) {
		char msg[64];
		snprintf(msg, sizeof msg, "present deferred at %dx%d @%.2f",
		         fAttached.size.x, fAttached.size.y, fAttached.scale);
		cl::crash::breadcrumb("canvas", msg);
	}
	fGate = Gate::Blocked;
	const unsigned generation = ++fGateGeneration;
	fCanvas->CallAfter([this, generation] {
		if (generation != fGateGeneration) return;
		fGate = Gate::Released;
		fCanvas->Refresh(false);
	});
}

void CanvasGL::paint(const std::function<bool()>& draw) {
	wxPaintDC dc(fCanvas);   // wx requires one in every paint handler, used or not
	const Current current = makeCurrent();
	if (current == Current::Failed) return;
	if (kSkipPresentOnReshape && current == Current::Reshaped) blockPresent();
	if (fGate == Gate::Blocked) return;
	if (fGate == Gate::Released) {
		cl::crash::breadcrumb("canvas", "presenting first frame after reshape");
		fGate = Gate::Open;
	}
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
