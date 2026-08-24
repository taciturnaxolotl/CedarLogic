// Text measurement for the browser, per the render/TextMetrics.h seam.
//
// Canvas2D is what draws the text here, so Canvas2D is what must measure it --
// anything else and a label's hit box drifts from its glyphs. measureText is
// synchronous, so this crosses into JavaScript and back within the call.
//
// The measuring context is created once and reused; creating a canvas per
// measurement would dominate the cost of laying out a circuit full of labels.
// It is resolved lazily and defensively: this module also runs in a worker
// (OffscreenCanvas, no document) and under Node (neither), and a gate's hit box
// gets computed the moment a circuit loads, long before anything is drawn.

#include <emscripten.h>

#include "render/TextMetrics.h"

// One measuring context, made once. A window has document.createElement; a
// worker has OffscreenCanvas and no document; Node has neither. Where there is
// no canvas at all the metrics fall back to a ratio of the font size, which is
// wrong in detail but keeps a headless render (tests, CI) from crashing.
EM_JS(void, cedar_init_text_measure, (), {
	if (Module.__textMeasure !== undefined) return;
	try {
		if (typeof OffscreenCanvas !== "undefined") {
			Module.__textMeasure = new OffscreenCanvas(1, 1).getContext("2d");
		} else if (typeof document !== "undefined") {
			Module.__textMeasure = document.createElement("canvas").getContext("2d");
		} else {
			Module.__textMeasure = null;
		}
	} catch (e) {
		Module.__textMeasure = null;
	}
});

EM_JS(float, cedar_measure_text_width, (const char *utf8, float pixelHeight), {
	cedar_init_text_measure();
	const ctx = Module.__textMeasure;
	const text = UTF8ToString(utf8);
	// Without a canvas, estimate from the average advance of a monospace-ish
	// face. Only headless callers land here.
	if (!ctx) return text.length * pixelHeight * 0.6;
	ctx.font = pixelHeight + "px " + (Module.sceneFont || "sans-serif");
	return ctx.measureText(text).width;
});

// Top of the capitals down past the descenders, matching what the Skia backend
// reports, so a circuit's hit boxes are the same size in both renderers.
EM_JS(float, cedar_measure_text_height, (float pixelHeight), {
	cedar_init_text_measure();
	const ctx = Module.__textMeasure;
	if (!ctx) return pixelHeight;
	ctx.font = pixelHeight + "px " + (Module.sceneFont || "sans-serif");
	const m = ctx.measureText("Hg");
	return m.actualBoundingBoxAscent + m.actualBoundingBoxDescent;
});

namespace cl {
namespace render {

float measuredTextWidth(const char *utf8, float pixelHeight) {
	if (!utf8 || !*utf8 || pixelHeight <= 0) return 0.0f;
	return cedar_measure_text_width(utf8, pixelHeight);
}

float measuredTextHeight(float pixelHeight) {
	if (pixelHeight <= 0) return 0.0f;
	return cedar_measure_text_height(pixelHeight);
}

}  // namespace render
}  // namespace cl
