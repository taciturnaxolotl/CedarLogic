#ifndef GL_DEFS_H_
#define GL_DEFS_H_

#include "gl_wrapper.h"

struct GLPoint2f {
	GLPoint2f( GLfloat newX = 0.0, GLfloat newY = 0.0 ) : x(newX), y(newY) {};
	GLfloat x, y;

	GLPoint2f operator+(const GLPoint2f &other) const;

	GLPoint2f operator-(const GLPoint2f &other) const;

	void operator+=(const GLPoint2f &other);

	void operator-=(const GLPoint2f &other);

	bool operator==(const GLPoint2f &other) const;

	bool operator!=(const GLPoint2f &other) const;
};

struct GLLine2f {
	GLPoint2f begin;
	GLPoint2f end;
};

// Background grid appearance. Shared by every renderer that draws it: the
// legacy GL canvas, CircuitPage's Scene path, and the oscope.
#define GRID_INTENSITY 0.08
#define MIN_GRID_SCREEN_SPACING 13

#define WIRE_BBOX_THICKNESS 0.25
#define DEG2RAD 0.0174533
#define EQUALRANGE 0.00125

#endif /*GL_DEFS_H_*/
