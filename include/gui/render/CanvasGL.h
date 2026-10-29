// CanvasGL -- one GL context and Skia backend per canvas, presented only through paint().
// On macOS a paint that reshapes the drawable does not present; it repaints later (CEDARLOGIC-5).

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
	// context current, run `draw`, then flush and swap (on macOS a reshape skips the
	// frame and schedules another). If `draw` returns false
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

	// Client size plus backing scale: the scale moves alone across displays.
	struct Shape {
		wxSize size;
		double scale = 0.0;   // no real canvas is zero-scale, so the first paint attaches
		bool operator==(const Shape& o) const { return size == o.size && scale == o.scale; }
	};
	enum class Current { Failed, Same, Reshaped };
	// Present gate after a reshape: Blocked until a later run-loop turn, then Released for one frame.
	enum class Gate { Open, Blocked, Released };

	bool ensureContext();
	// Re-attaches only on a new shape; private so nothing makes current outside paint().
	Current makeCurrent();
	void blockPresent();

	wxGLCanvas* fCanvas;
	wxGLContext* fContext;
	Shape fAttached;
	bool fContextFailed;   // creation failed once; do not retry every frame
	Gate fGate = Gate::Open;
	unsigned fGateGeneration = 0;   // a stale release from an earlier reshape is ignored
#ifdef WITH_SKIA
	SkiaBackend* fSkia;    // owned; destroyed with the GL context current
#endif
};

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_CANVASGL_H
