// Force-included when building the collision unit test. The collision checker
// and klsBBox only need the GLfloat type from the GL wrapper -- not OpenGL
// itself -- so this defines GLfloat and suppresses gl_wrapper.h (which drags in
// <GL/gl.h>, <GL/glu.h>, and windows.h). That keeps the test buildable on a
// headless CI runner with no GL/GLU dev headers.
#ifndef CEDAR_COLLISION_TEST_GL_SHIM_H
#define CEDAR_COLLISION_TEST_GL_SHIM_H

#define GLWRAPPER_H  // stop gl_wrapper.h from including the real GL/OS headers
typedef float GLfloat;

#endif
