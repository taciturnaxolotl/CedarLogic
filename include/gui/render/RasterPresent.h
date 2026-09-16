// RasterPresent -- put a CPU-drawn image into the window, using only the OpenGL
// that every driver has.
//
// This is the second half of the software fallback. When the graphics driver is
// too old for the drawing engine to use it (a virtual machine with no 3D, most
// often), the frame is drawn on the CPU instead, and then it still has to reach
// the screen. glDrawPixels is OpenGL 1.1, so it is available even on Windows'
// software fallback driver, which is the whole point.
//
// This needs a compatibility GL context, since a core profile removes the fixed
// pipeline this uses. That costs nothing: a driver modern enough to be core-only
// is modern enough that this path never runs.
//
// Kept in its own file with no Skia headers, because the GL headers drag in
// windows.h and Skia does not get along with it.

#ifndef CL_RENDER_RASTERPRESENT_H
#define CL_RENDER_RASTERPRESENT_H

namespace cl {
namespace render {

// Copy a tightly packed RGB image into the framebuffer that is currently bound,
// filling it. Rows run bottom to top, the order OpenGL reads them in. The caller
// presents with SwapBuffers as usual.
void presentRGB(int width, int height, const unsigned char* rgbBottomUp);

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_RASTERPRESENT_H
