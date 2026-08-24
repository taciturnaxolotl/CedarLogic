// Text measurement for the browser, per the render/TextMetrics.h seam.
//
// Canvas2D is what draws the text here, so Canvas2D is what must measure it --
// anything else and a label's hit box drifts from its glyphs. measureText is
// synchronous, so this crosses into JavaScript and back within the call.
//
// The measuring context is created once and reused; creating a canvas per
// measurement would dominate the cost of laying out a circuit full of labels.

#include <emscripten.h>

#include "render/TextMetrics.h"

EM_JS(float, cedar_measure_text_width, (const char *utf8, float pixelHeight), {
	const ctx = Module.__textMeasureCtx ||
	            (Module.__textMeasureCtx =
	                 document.createElement("canvas").getContext("2d"));
	ctx.font = pixelHeight + "px " + (Module.sceneFont || "sans-serif");
	return ctx.measureText(UTF8ToString(utf8)).width;
});

// Top of the capitals down past the descenders, matching what the Skia backend
// reports, so a circuit's hit boxes are the same size in both renderers.
EM_JS(float, cedar_measure_text_height, (float pixelHeight), {
	const ctx = Module.__textMeasureCtx ||
	            (Module.__textMeasureCtx =
	                 document.createElement("canvas").getContext("2d"));
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
