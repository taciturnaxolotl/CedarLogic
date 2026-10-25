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
	return nearly(x, other.x) && nearly(y, other.y);
}

bool GLPoint2f::operator!=(const GLPoint2f &other) const {
	return !(*this == other);
}

SinCos sinCosDegrees(double degrees) {
	const double turns = degrees / 90.0;
	if (turns == std::floor(turns)) {
		static const double kSin[4] = {0, 1, 0, -1};
		const int quarter = (int)(((long long)turns % 4 + 4) % 4);
		return { kSin[quarter], kSin[(quarter + 1) % 4] };
	}
	return { std::sin(degrees * DEG2RAD), std::cos(degrees * DEG2RAD) };
}
