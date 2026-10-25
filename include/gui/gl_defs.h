#ifndef GL_DEFS_H_
#define GL_DEFS_H_

#include "gl_wrapper.h"

#include <cmath>

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

#define WIRE_BBOX_THICKNESS 0.25
#define DEG2RAD 0.017453292519943295
#define EQUALRANGE 0.00125

// The one definition of "the same coordinate" for wire geometry.
inline bool nearly(float a, float b) { return std::fabs(a - b) <= EQUALRANGE; }

// Whether spans a0..a1 and b0..b1, ends in either order, overlap or meet.
inline bool spansTouch(float a0, float a1, float b0, float b1) {
	return std::fmin(a0, a1) <= std::fmax(b0, b1) + EQUALRANGE &&
	       std::fmin(b0, b1) <= std::fmax(a0, a1) + EQUALRANGE;
}

struct SinCos { double sin, cos; };

// Exact on quarter turns, so a turned gate's pins stay on the grid lines.
SinCos sinCosDegrees(double degrees);

#endif /*GL_DEFS_H_*/
