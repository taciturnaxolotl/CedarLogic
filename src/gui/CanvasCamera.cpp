/*****************************************************************************
   Project: CEDAR Logic Simulator

   CanvasCamera: pan, zoom, and grid snapping, with no toolkit attached.
*****************************************************************************/

#include "CanvasCamera.h"

#include <algorithm>
#include <cmath>

void CanvasCamera::setPan(GLdouble newX, GLdouble newY) {
	newX = std::max(newX, (GLdouble)MIN_PAN);
	newX = std::min(newX, (GLdouble)MAX_PAN);
	newY = std::max(newY, (GLdouble)MIN_PAN);
	newY = std::min(newY, (GLdouble)MAX_PAN);

	fPanX = newX;
	fPanY = newY;

	// The pointer has not moved, but the world under it has, so anything
	// tracking the cursor is now stale. The host decides how much of that work
	// is worth doing mid-pan.
	if (fHost) fHost->cameraPointerFollowed();

	requestRepaint();
}

void CanvasCamera::setZoom(GLdouble newZoom) {
	newZoom = std::max(newZoom, (GLdouble)MIN_ZOOM);
	newZoom = std::min(newZoom, (GLdouble)MAX_ZOOM);

	// Hold the centre of the view fixed while the scale changes: measure where
	// the centre sits relative to the corner, rescale that offset, and pan by
	// the difference.
	GLPoint2f center = getCenter();
	GLPoint2f topLeft, bottomRight;
	getViewport(topLeft, bottomRight);

	GLPoint2f oldDist = center - topLeft;
	GLPoint2f newDist = oldDist;

	oldDist.x *= newZoom / fZoom;
	oldDist.y *= newZoom / fZoom;

	fZoom = newZoom;

	translatePan(newDist.x - oldDist.x, newDist.y - oldDist.y);
}

void CanvasCamera::zoomToPoint(double steps, GLPoint2f point) {
	if (steps == 0.0) return;

	GLPoint2f center = getCenter();

	// The cursor's offset from the centre, in device pixels -- the quantity that
	// must survive the zoom if the point under the cursor is to stay put.
	GLPoint2f centerToPoint = point - center;
	centerToPoint.x /= fZoom;
	centerToPoint.y /= fZoom;

	beginCompoundMove();
	// Zooming in and out were written as separate branches, but dividing by
	// pow(s, -n) is multiplying by pow(s, n) -- one expression covers both, and
	// unlike the branches it means something for a fractional step.
	setZoom(fZoom * std::pow((GLdouble)ZOOM_STEP, (GLdouble)steps));

	centerToPoint.x *= fZoom;
	centerToPoint.y *= fZoom;

	setCenter(point.x - centerToPoint.x, point.y - centerToPoint.y);
	endCompoundMove();
}

void CanvasCamera::setViewport(GLPoint2f topLeft, GLPoint2f bottomRight) {
	if (!fHost) return;
	const double w = fHost->cameraViewportWidth();
	const double h = fHost->cameraViewportHeight();
	if (w <= 0 || h <= 0) return;

	const double sAspect = h / w;

	const double newWidth = bottomRight.x - topLeft.x;
	const double newHeight = topLeft.y - bottomRight.y;
	const double aspect = newHeight / newWidth;

	const bool useWidth = aspect < sAspect;  // width is the limiting factor

	GLdouble newZoom = 1.0;
	GLPoint2f newPan;

	if (useWidth) {
		newZoom = newWidth / w;
		// The x coordinate is the edge of the box; y must centre it.
		newPan.x = topLeft.x;
		newPan.y = topLeft.y + 0.5 * (h * newZoom - newHeight);
	} else {
		newZoom = newHeight / h;
		// The y coordinate is the edge of the box; x must centre it.
		newPan.y = topLeft.y;
		newPan.x = topLeft.x - 0.5 * (w * newZoom - newWidth);
	}

	setZoom(newZoom);
	setPan(newPan.x, newPan.y);
}

void CanvasCamera::getViewport(GLPoint2f& topLeft, GLPoint2f& bottomRight) const {
	const int w = fHost ? fHost->cameraViewportWidth() : 0;
	const int h = fHost ? fHost->cameraViewportHeight() : 0;
	topLeft.x = fPanX;
	topLeft.y = fPanY;
	bottomRight.x = fPanX + (w * fZoom);
	bottomRight.y = fPanY - (h * fZoom);
}

GLPoint2f CanvasCamera::getCenter() const {
	GLPoint2f topLeft, bottomRight;
	getViewport(topLeft, bottomRight);

	GLPoint2f center = bottomRight + topLeft;
	center.x /= 2;
	center.y /= 2;
	return center;
}

void CanvasCamera::setCenter(GLdouble newX, GLdouble newY) {
	GLPoint2f topLeft, bottomRight;
	getViewport(topLeft, bottomRight);
	const GLPoint2f center = getCenter();

	setPan(newX - (center.x - topLeft.x), newY - (center.y - topLeft.y));
}

GLPoint2f CanvasCamera::mapToWorld(int px, int py) const {
	return GLPoint2f((float)(fPanX + (px * fZoom)),
	                 (float)(fPanY - (py * fZoom)));
}

GLPoint2f CanvasCamera::getSnappedPoint(GLPoint2f c) const {
	const GLfloat x = fHorizSpacing * std::floor(c.x / fHorizSpacing + 0.5f);
	const GLfloat y = fVertSpacing * std::floor(c.y / fVertSpacing + 0.5f);
	return GLPoint2f(x, y);
}
