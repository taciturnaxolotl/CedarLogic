// Headless GL types for the WebAssembly build.
//
// Nothing in the ported layer calls OpenGL any more -- every draw site emits
// into cl::render::Scene (Workstream G) -- but the geometry types still spell
// themselves GLfloat/GLdouble. Defining GLWRAPPER_H suppresses gl_wrapper.h so
// the real GL/OS headers never come in, and these typedefs stand in for the
// handful of scalar types that remain.
#ifndef CEDAR_WASM_GL_SHIM_H
#define CEDAR_WASM_GL_SHIM_H

#define GLWRAPPER_H

typedef float        GLfloat;
typedef double       GLdouble;
typedef int          GLint;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int          GLsizei;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef void         GLvoid;

#endif
