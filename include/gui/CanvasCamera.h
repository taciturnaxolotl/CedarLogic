/*****************************************************************************
   Project: CEDAR Logic Simulator

   CanvasCamera: pan, zoom, and grid snapping, with no toolkit attached.
*****************************************************************************/

// Where the view sits, how far one notch of the wheel zooms, whether a zoom
// pivots on the cursor or the centre, where a point lands when it snaps -- this
// is half the feel of the application, and it is arithmetic. None of it needs a
// window.
//
// So it lives here, and klsGLCanvas keeps a CanvasCamera rather than being one.
// The browser canvas keeps the same object, which is what makes panning and
// zooming there identical rather than merely similar.
//
// The camera does the arithmetic and says when something changed; the host
// decides what that means -- when to repaint, how to throttle, what the
// viewport measures. See CameraHost.

#ifndef CANVASCAMERA_H_
#define CANVASCAMERA_H_

#include "gl_defs.h"

#define MIN_ZOOM 1.0/120.0
#define MAX_ZOOM 1.0*1.0
#define DEFAULT_ZOOM 1.0/10.0

// The amount of zooming done per step (in %).
#define ZOOM_STEP 0.75

#define MIN_PAN -1.0e10
#define MAX_PAN 1.0e10

// The amount of panning done per step for keypress (in pixels).
#define PAN_STEP 30

// The amount of panning done per step for autoscroll (in pixels).
#define SCROLL_STEP 30

// What a camera needs from whatever is showing it. Everything here is a policy
// question the camera has no business answering: how big the view is, when to
// paint, and what a moved camera means for the pointer under it.
class CameraHost {
public:
	virtual ~CameraHost() {}

	// The viewport, in device pixels. Asked for rather than stored, so a resize
	// needs no notification to stay correct.
	virtual int cameraViewportWidth() const = 0;
	virtual int cameraViewportHeight() const = 0;

	// The camera settled somewhere new: repaint. The host owns the throttling --
	// a mouse fires moves faster than anything can paint.
	virtual void cameraRepaint() {}

	// The camera moved, so the world point under the pointer changed even though
	// the pointer did not. Whatever tracks the cursor has to follow it.
	virtual void cameraPointerFollowed() {}
};

class CanvasCamera {
public:
	explicit CanvasCamera(CameraHost* host = nullptr)
		: fHost(host), fZoom(DEFAULT_ZOOM), fPanX(0), fPanY(0),
		  fHorizSpacing(1), fVertSpacing(1), fCompoundDepth(0) {}

	void setHost(CameraHost* host) { fHost = host; }

	// --- pan ---------------------------------------------------------------

	void getPan(GLdouble& x, GLdouble& y) const { x = fPanX; y = fPanY; }
	void setPan(GLdouble newX, GLdouble newY);
	void translatePan(GLdouble relX, GLdouble relY) { setPan(fPanX + relX, fPanY + relY); }

	// --- zoom --------------------------------------------------------------

	GLdouble getZoom() const { return fZoom; }
	void setZoom(GLdouble newZoom);

	// Zoom by `steps`, keeping `point` (a world point) under the cursor.
	// Positive zooms in. Steps may be fractional: one whole step is ZOOM_STEP,
	// so a wheel notch is 1 and a trackpad can pass the fraction of a step its
	// travel is worth, which is the difference between zoom that jumps and zoom
	// that glides.
	void zoomToPoint(double steps, GLPoint2f point);

	// One or more whole wheel notches. What the desktop's wheel handler calls.
	void zoomToMouse(long numLines, GLPoint2f mouse) {
		zoomToPoint((double)numLines, mouse);
	}

	// --- viewport ----------------------------------------------------------

	// Fit a world rectangle to the view. Enforces a 1:1 aspect ratio: the longer
	// side fits the window and the other is centred.
	void setViewport(GLPoint2f topLeft, GLPoint2f bottomRight);
	void getViewport(GLPoint2f& topLeft, GLPoint2f& bottomRight) const;

	GLPoint2f getCenter() const;
	void setCenter(GLdouble newX, GLdouble newY);

	// A point in device pixels (origin top-left) to world coordinates.
	GLPoint2f mapToWorld(int px, int py) const;

	// The viewport, as the host measures it. Renderers need this to know what
	// slice of the world is on screen.
	int viewportWidth() const { return fHost ? fHost->cameraViewportWidth() : 0; }
	int viewportHeight() const { return fHost ? fHost->cameraViewportHeight() : 0; }

	// --- grid --------------------------------------------------------------

	void setGridSpacing(GLfloat horiz, GLfloat vert) {
		if (horiz != 0.0f) fHorizSpacing = horiz;
		if (vert != 0.0f) fVertSpacing = vert;
	}
	GLfloat horizSpacing() const { return fHorizSpacing; }
	GLfloat vertSpacing() const { return fVertSpacing; }

	// The nearest grid point.
	GLPoint2f getSnappedPoint(GLPoint2f c) const;

	// --- compound moves ----------------------------------------------------

	// A zoom is a setZoom plus a setCenter, each of which moves the pan. Bracket
	// them so the host paints once at the final state rather than flashing the
	// intermediate frame. Nests.
	void beginCompoundMove() { ++fCompoundDepth; }
	void endCompoundMove() {
		if (fCompoundDepth > 0 && --fCompoundDepth == 0) requestRepaint();
	}

private:
	void requestRepaint() {
		if (fCompoundDepth == 0 && fHost) fHost->cameraRepaint();
	}

	CameraHost* fHost;

	GLdouble fZoom;            // world units per device pixel
	GLdouble fPanX, fPanY;     // world coordinate of the view's top-left corner

	GLfloat fHorizSpacing;
	GLfloat fVertSpacing;

	int fCompoundDepth;
};

#endif /*CANVASCAMERA_H_*/
