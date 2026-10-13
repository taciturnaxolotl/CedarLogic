// CanvasGL -- one GL context, and one Skia backend on it, per canvas.
//
// wxGLContext::SetCurrent is three calls on macOS, not one:
//
//     [context setView: canvas]; [context update]; [context makeCurrentContext];
//
// The app used to share a single context between the main canvas, the minimap
// and the oscope, so every paint of every canvas re-pointed that context at a
// different NSView and re-attached its drawable. Usually harmless. But the
// window server can drive a redisplay synchronously, from inside the
// CoreAnimation transaction that is itself resizing the view: tile a window and
// AppKit posts a tile-resize notification, commits a transaction in the handler,
// and that forces a display on the GL backing layer, straight into our paint
// handler. Re-attaching a drawable there and then presenting it asks AppKit to
// flush a surface whose geometry is half-updated, which it did by copying out of
// bounds (EXC_BAD_ACCESS in memmove under -[NSCGLSurface flushRect:], macOS
// 26.2).
//
// So: a context per canvas, never re-pointed, and a drawable re-attached only
// when the canvas actually changed shape. makeCurrent() is then just "make it
// current" on every frame that did not resize, which is nearly all of them.
//
// The Skia backend rides along because a GrDirectContext is bound to the GL
// context it was built on; neither can outlive or migrate off the other.

#ifndef CL_RENDER_CANVASGL_H
#define CL_RENDER_CANVASGL_H

#include <functional>

#include "wx/glcanvas.h"

namespace cl {
namespace render {

class SkiaBackend;

class CanvasGL {
public:
	// `canvas` must outlive this object. `smallCache` asks for a smaller Ganesh
	// resource budget, for the canvases that draw a thumbnail or a waveform
	// rather than a whole circuit.
	explicit CanvasGL(wxGLCanvas* canvas, bool smallCache = false);
	~CanvasGL();

	// One frame, start to finish, and the only way a canvas presents: make the
	// context current, run `draw`, then flush and swap. If `draw` returns false
	// the frame is cleared to white rather than presenting an undefined buffer.
	// Call only from an EVT_PAINT handler; it constructs the wxPaintDC.
	void paint(const std::function<bool()>& draw);

#ifdef WITH_SKIA
	// Valid inside a paint() callback, where this canvas's context is current.
	SkiaBackend& skia();
#endif

private:
	CanvasGL(const CanvasGL&);
	CanvasGL& operator=(const CanvasGL&);

	bool ensureContext();
	// Make the context current, re-attaching the drawable only if the canvas
	// changed shape. Private so that nothing can make current without presenting.
	bool makeCurrent();

	wxGLCanvas* fCanvas;
	wxGLContext* fContext;
	// Canvas shape as of the last drawable attach. Both halves matter: the
	// backing scale moves on its own when a window crosses to a display of a
	// different density without changing size. The initial zero scale is a shape
	// no real canvas has, so the first makeCurrent() always attaches.
	wxSize fAttachedSize;
	double fAttachedScale;
	bool fContextFailed;   // creation failed once; do not retry every frame
#ifdef WITH_SKIA
	SkiaBackend* fSkia;    // owned; destroyed with the GL context current
#endif
};

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_CANVASGL_H
