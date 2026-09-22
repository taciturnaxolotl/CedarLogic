// This file includes opengl whether on windows, mac, or linux.
// 10/1/2016 - Tyler J. Drake

#ifndef GLWRAPPER_H
#define GLWRAPPER_H

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#ifdef _WIN32
// windows.h defines min/max as macros, which breaks std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
// Linux needs GL 3.0+ for framebuffer extension functions
#ifdef __linux__
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glu.h>
#ifdef __linux__
#include <GL/glext.h>
#endif
#endif

#endif