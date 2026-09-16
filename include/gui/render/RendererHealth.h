// RendererHealth -- what the machine's OpenGL turned out to be, and whether the
// drawing engine could use it.
//
// The failure this exists for: a machine with no graphics driver (a virtual
// machine without 3D acceleration is the usual one) falls back to a software
// OpenGL from the 1990s. A context comes up, so nothing looks wrong, but Skia
// needs shaders and refuses it. Every frame then quietly declines and the
// canvas clears white -- which, on a circuit you have not drawn yet, is exactly
// what a working canvas looks like. People report "it opened but I cannot place
// any gates" and there is nothing in the app to contradict that.
//
// So: record what the driver calls itself, record why the engine gave up, and
// let the app say so out loud instead of leaving a blank rectangle to explain
// itself. Deliberately free of Skia headers, so the reporting path stays
// available even in a build where the engine is not.

#ifndef CL_RENDER_RENDERERHEALTH_H
#define CL_RENDER_RENDERERHEALTH_H

#include <string>

namespace cl {
namespace render {

// Whether a GL implementation naming itself this way is a software stand-in
// rather than a graphics card. Pure, so it can be tested without a machine
// broken enough to produce one honestly. An empty renderer counts as software:
// a driver that will not even say its own name is not one to draw with.
bool looksLikeSoftwareGL(const std::string& renderer, const std::string& version);

// Record what the GL driver calls itself. Needs a current GL context; cheap to
// call on every frame, since only the first call with a non-empty renderer
// sticks. Null arguments are tolerated: a driver may answer nothing.
void noteGLImplementation(const char* vendor, const char* renderer,
                          const char* version);

// One line naming the GL implementation, e.g. "GDI Generic (Microsoft
// Corporation), OpenGL 1.1.0". "unknown" before noteGLImplementation lands.
std::string glImplementation();

// Record that the engine will not draw to the window, and why, in engine terms
// ("no GL context for Ganesh"). Only the first reason is kept: after the first
// failure everything downstream fails too, and the first one is the cause.
void noteRendererFailure(const char* what);

// True once the engine has given up on the window.
bool rendererFailed();

// The internal reason, for logs. Empty while healthy.
const std::string& rendererFailureReason();

// The same news written for a person, naming the driver and what to do about
// it. Empty while healthy.
std::string rendererFailureMessage();

// Test hook: with CEDAR_FORCE_GL_FAILURE set in the environment, the engine
// refuses the GL context even on a machine that has a good one. This is how the
// blank-canvas path gets exercised without finding a machine broken enough to
// produce it honestly.
bool forceGLFailure();

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_RENDERERHEALTH_H
