// RasterPresent -- see RasterPresent.h.

#include "render/RasterPresent.h"

#include "gl_wrapper.h"

namespace cl {
namespace render {

void presentRGB(int width, int height, const unsigned char* rgbBottomUp) {
	if (width <= 0 || height <= 0 || rgbBottomUp == nullptr) return;

	// Skia left the shared GL context in whatever state its last frame needed,
	// and so did the rest of the app. Put back the few pieces this depends on.
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   // rows are packed, not padded to 4
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glPixelZoom(1.0f, 1.0f);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);   // glDrawPixels multiplies by this

	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, width, 0, height, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// The bottom-left corner. A raster position outside the viewport makes GL
	// throw the whole image away, and the corners are exactly where that bites,
	// so the image is handed over bottom-up and starts from the safe corner.
	glRasterPos2i(0, 0);
	glDrawPixels(width, height, GL_RGB, GL_UNSIGNED_BYTE, rgbBottomUp);

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

}  // namespace render
}  // namespace cl
