/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsGLCanvas: Generic implementation of OpenGL canvas
*****************************************************************************/

#ifndef KLS_GL_CANVAS_H_
#define KLS_GL_CANVAS_H_

class klsGLCanvas;

#include "MainApp.h"
#include "InputEvent.h"
#include "CanvasCamera.h"
#include "wx/glcanvas.h"
class klsMiniMap;   // pointer member only; the minimap header pulls in wx DCs
// For GLPoint2f:

// Included for floor() method:
#include <cmath>

#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <deque>
using namespace std;

// Zoom, pan, and snapping constants live with the camera that applies them.
#define SCROLL_TIMER_RATE 30
#define SCROLL_TIMER_ID 1


enum mouseButton {
	BUTTON_LEFT = 0,
	BUTTON_MIDDLE,
	BUTTON_RIGHT,
	NUM_BUTTONS
};

// The camera half of this class -- pan, zoom, snapping -- moved to CanvasCamera,
// which carries no toolkit. What remains here is the window: the GL context, the
// wx event plumbing, and the repaint policy the camera asks for through
// CameraHost.
class klsGLCanvas: public wxGLCanvas, public CameraHost
{

public:
    klsGLCanvas( wxWindow *parent,
		const wxString& name = "klsGLCanvas",
		wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0 );

    virtual ~klsGLCanvas();


	//TODO: Add some scrollbars and some methods for setting the usable canvas size.

	// Handle events from wxWidgets:
    void wxOnPaint(wxPaintEvent& event);
    void wxOnCaptureLost(wxMouseCaptureLostEvent& event);
    void wxOnSize(wxSizeEvent& event);
    void wxOnEraseBackground(wxEraseEvent& event);

    void wxOnMouseEvent(wxMouseEvent& event);
    void wxOnMouseWheel(wxMouseEvent& event);

    void wxKeyDown(wxKeyEvent& event);
	void wxKeyUp(wxKeyEvent& event);


	// Send events to subclassed canvas.
	//
	// These take toolkit-neutral events, not wxMouseEvent: what a subclass does
	// with the mouse IS the feel of the application, and it should be written
	// once and compiled everywhere rather than reimplemented per platform. The
	// wx handlers above are the only translation site; a browser shell drives
	// the same methods from DOM events. Positions are already in world
	// coordinates, mapped through the camera by the translator.
	virtual void OnMouseDown( const input::PointerEvent& event ) {};
	virtual void OnMouseUp( const input::PointerEvent& event ) {};
	virtual void OnMouseMove( const input::PointerEvent& event ) {};
	virtual void OnMouseEnter( const input::PointerEvent& event ) {};
	virtual void OnMouseLeave( const input::PointerEvent& event ) {};

	// Default OnMouseWheel handler simply zooms using the mouse wheel:
	virtual void OnMouseWheel( long numOfLines );

	// Return true if the subclass consumed the key, which stops this class from
	// applying its own pan/zoom bindings to it.
	virtual bool OnKeyDown( const input::KeyEvent& event ) { return false; };
	virtual void OnKeyUp( const input::KeyEvent& event ) {};

	// Render the live frame through Skia's Ganesh GL backend. Returns true if it
	// painted (caller then just SwapBuffers). Base is a no-op.
	virtual bool renderSkiaLive() { return false; }
	// Unwind any subclass-specific drag state on an abnormal drag end (lost mouse
	// capture). endDrag() only clears the base flags; subclasses that track their
	// own drag (new-gate placement, paste, gate move) override this to cancel it.
	virtual void cancelDrag() {}
	virtual void OnSize(void) {};

	// Event query methods:
	// (Need member vars to back these up, too.)
	GLPoint2f getDragStartCoords( mouseButton whichButton = BUTTON_LEFT ) { return dragStartCoords[whichButton]; };
	GLPoint2f getMouseCoords(void) { return mouseCoords; };
	bool isMouseInWindow(void) { return !mouseOutOfWindow; }
	GLPoint2f getDragEndCoords( mouseButton whichButton = BUTTON_LEFT ) { return dragEndCoords[whichButton]; };
	bool isDragging(  mouseButton whichButton = BUTTON_LEFT ) { return isDraggingFlag[whichButton]; };
	
	// Return the point snapped to the nearest grid point:
	GLPoint2f getSnappedPoint( GLPoint2f c );

	void updateMiniMap(void);
	
	// Editing control functions
	void lockCanvas(void) { canvasLocked = true; }
	void unlockCanvas(void) { canvasLocked = false; }
	bool isLocked(void) { return canvasLocked; }

	// Event creation methods:
	// Start a drag event right away, using the current mouse coordinates.
	// This captures the mouse using CaptureMouse() and sets the "Drag Start Coords"
	// to the current mouse coordinates.
	// (This is usually called by this class right before OnMouseDown(), but
	// can be called by the subclasses. For example, right after an OnMouseEnter()
	// in which a gate is being dragged. Or, maybe also for a Paste from clipboard event.)
	void beginDrag(mouseButton whichButton = BUTTON_LEFT);

	// Force the drag event to end, by unclaiming the mouse (If all other buttons haven't
	// claimed a drag event too) and setting the "Drag End Coords".
	void endDrag(mouseButton whichButton = BUTTON_LEFT);

	// Zoom and pan. These forward to the camera; they stay on the canvas so the
	// hundreds of existing call sites read the same as they always did.
	// The camera this window owns. GUICanvas points its page at this one, so a
	// canvas and the page it shows never disagree about where the view is.
	CanvasCamera& canvasCamera() { return camera; }
	void getPan(GLdouble &x, GLdouble &y) { camera.getPan(x, y); }
	void setPan(GLdouble newX, GLdouble newY) { camera.setPan(newX, newY); }
	void setCenter(GLdouble newX, GLdouble newY) { camera.setCenter(newX, newY); }
	void translatePan(GLdouble relX, GLdouble relY) { camera.translatePan(relX, relY); }
	void OnScrollTimer(wxTimerEvent& event);

	GLdouble getZoom() { return camera.getZoom(); };
	void setZoom(GLdouble newZoom) { camera.setZoom(newZoom); }
	void zoomToMouse(long); //Julian
	GLPoint2f getCenter() { return camera.getCenter(); } //Julian

	// Grid background:
	// (Can turn grid back on without changing the past
	// setting by calling it with no params.)
	void setHorizGrid(GLfloat hSpacing = 0.0);
	void setHorizGridColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
	void disableHorizGrid(void);

	void setVertGrid(GLfloat vSpacing = 0.0);
	void setVertGridColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
	void disableVertGrid(void);

	bool horizOn = true; // Horizontal grid lines on/off.
	GLfloat hColor[4]; // Horizontal grid color.

	bool vertOn = true;  // Vertical grid lines on/off.
	GLfloat vColor[4]; // Vertical grid color.

	// Set the viewport (Set the left/top and right/bottom coordinates).
	// NOTE: It will enforce a 1:1 aspect ratio, but it will make the best
	// attempt to fit the zoom box as close as possible. Basically, it will
	// fit the longest side to the window, and center the rest.
	void setViewport(GLPoint2f topLeft, GLPoint2f bottomRight) {
		camera.setViewport(topLeft, bottomRight);
	}

	// Retrieves the current viewport (left/top and right/bottom)
	void getViewport(GLPoint2f& p1, GLPoint2f& p2) { camera.getViewport(p1, p2); }

	// map a point in surface local coordinates to coordinates on the canvas
	GLPoint2f mapToCanvas(wxPoint m);

	void autoScrollEnable(void) { autoScrollActive = true; };
	void autoScrollDisable(void) { autoScrollActive = false; };
	bool isAutoScrollOn(void) { return autoScrollActive; };

	// CameraHost -- the policy the camera asks this window for.
	int cameraViewportWidth() const override;
	int cameraViewportHeight() const override;
	void cameraRepaint() override;
	void cameraPointerFollowed() override;

protected:
	// The minimap associated with this canvas
	klsMiniMap* minimap;

private:
	wxPoint mouseScreenCoords;
	wxPoint getMouseScreenCoords(void) { return mouseScreenCoords;  }

	void setMouseScreenCoords( wxPoint newCoords ) { mouseScreenCoords = newCoords; }


	GLPoint2f dragStartCoords[NUM_BUTTONS];
	void setDragStartCoords( GLPoint2f newCoords, mouseButton whichButton = BUTTON_LEFT ) { dragStartCoords[whichButton] = newCoords; };

	GLPoint2f mouseCoords;
	void setMouseCoords( GLPoint2f newCoords ) { mouseCoords = newCoords; };

	// Set the mouse coords by converting last known screen coords:
	void setMouseCoords();

	GLPoint2f dragEndCoords[NUM_BUTTONS];
	void setDragEndCoords( GLPoint2f newCoords, mouseButton whichButton = BUTTON_LEFT ) { dragEndCoords[whichButton] = newCoords; };

	bool isDraggingFlag[NUM_BUTTONS];
	void setIsDragging( bool isDragging, mouseButton whichButton = BUTTON_LEFT ) { isDraggingFlag[whichButton] = isDragging; };

	bool deferPaint;    // suppress setPan's synchronous repaint during a compound
	                    // camera move (e.g. zoom = setZoom + setCenter) so it
	                    // paints once at the final state, not the intermediate one
	bool panning;       // a middle-drag pan is in progress: skip setPan's
	                    // hover/collision OnMouseMove work, which else runs twice
	                    // per move and stutters the drag on large circuits
	wxLongLong lastPanPaintMs;  // last synchronous repaint during a pan; used to
	                            // throttle painting to ~frame rate so a real
	                            // mouse's move flood doesn't back up the paints

	// Pan, zoom, and grid spacing live on the camera.
	CanvasCamera camera;

	
	// Scrolling timer used to auto-scroll the canvas when dragged outside of the
	// window:
	wxTimer* scrollTimer;
	bool autoScrollActive;

	// A variable to describe whether or not the mouse cursor is outside of the window:
	bool mouseOutOfWindow;
	
	// The accumulated mouse wheel rotation:
	double wheelRotation;
	
	// Key Control Flags
	bool isShiftDown;
	bool isControlDown;
	
	// Flag for edit control
	bool canvasLocked;

	DECLARE_EVENT_TABLE()

};

#endif /*KLS_GL_CANVAS_H_*/
