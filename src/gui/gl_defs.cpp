#include <cmath>
#include "gl_defs.h"


GLPoint2f GLPoint2f::operator+(const GLPoint2f &other) const {
	return GLPoint2f(x + other.x, y + other.y);
}

GLPoint2f GLPoint2f::operator-(const GLPoint2f &other) const {
	return GLPoint2f(x - other.x, y - other.y);
}

void GLPoint2f::operator+=(const GLPoint2f &other) {
	x += other.x;
	y += other.y;
}

void GLPoint2f::operator-=(const GLPoint2f &other) {
	x -= other.x;
	y -= other.y;
}

bool GLPoint2f::operator==(const GLPoint2f& other) const {
	return (x >= other.x - EQUALRANGE &&
		x <= other.x + EQUALRANGE &&
		y <= other.y + EQUALRANGE &&
		y >= other.y - EQUALRANGE);
}

bool GLPoint2f::operator!=(const GLPoint2f &other) const {
	return !(*this == other);
}
