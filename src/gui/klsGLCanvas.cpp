/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsGLCanvas: Generic implementation of OpenGL canvas
*****************************************************************************/

#include "klsGLCanvas.h"
#include <cstdlib>
#include "Settings.h"
#include "MainApp.h"
#include "paramDialog.h"
#include "GLFont/glfont2.h"

// Included to use the min() and max() templates:
#include <algorithm>
using namespace std;

// Activate this #define statement if you want to
// show the mouse handling arrows:
//#define CANVAS_DEBUG_TESTS_ON


DECLARE_APP(MainApp)

BEGIN_EVENT_TABLE(klsGLCanvas, wxGLCanvas)
    EVT_PAINT(klsGLCanvas::wxOnPaint)
    EVT_SIZE(klsGLCanvas::wxOnSize)
    EVT_ERASE_BACKGROUND(klsGLCanvas::wxOnEraseBackground)

	EVT_MOUSEWHEEL(klsGLCanvas::wxOnMouseWheel)
    EVT_MOUSE_EVENTS(klsGLCanvas::wxOnMouseEvent)
    EVT_MOUSE_CAPTURE_LOST(klsGLCanvas::wxOnCaptureLost)

    EVT_KEY_DOWN(klsGLCanvas::wxKeyDown)
    EVT_KEY_UP(klsGLCanvas::wxKeyUp)

   	EVT_TIMER(SCROLL_TIMER_ID, klsGLCanvas::OnScrollTimer)
END_EVENT_TABLE()


klsGLCanvas::klsGLCanvas(wxWindow *parent, const wxString& name, wxWindowID id,
						const wxPoint& pos, const wxSize& size, long style ) :
						wxGLCanvas(parent, id, NULL, pos, size, style|wxFULL_REPAINT_ON_RESIZE|wxWANTS_CHARS, name) {

	// Zoom and OpenGL coordinate of upper-left corner of this canvas:
	viewZoom = DEFAULT_ZOOM;
	panX = panY = 0.0;

	autoScrollEnable();
	
	// The mouse is in the window:
	mouseOutOfWindow = false;

	// Mouse wheel rotation tracking variable:
	wheelRotation = 0.0;

	// Set the mouse coords memory:
	setMouseCoords( GLPoint2f(0.0,0.0) );
	setMouseScreenCoords( wxPoint( 0, 0 ) );

	setIsDragging( false, BUTTON_LEFT );
	setDragStartCoords( GLPoint2f(0.0,0.0), BUTTON_LEFT );
	setDragEndCoords( GLPoint2f(0.0,0.0), BUTTON_LEFT );

	setIsDragging( false, BUTTON_MIDDLE );
	setDragStartCoords( GLPoint2f(0.0,0.0), BUTTON_MIDDLE );
	setDragEndCoords( GLPoint2f(0.0,0.0), BUTTON_MIDDLE );

	setIsDragging( false, BUTTON_RIGHT );
	setDragStartCoords( GLPoint2f(0.0,0.0), BUTTON_RIGHT );
	setDragEndCoords( GLPoint2f(0.0,0.0), BUTTON_RIGHT );

	// Set up scrolling timer:
	scrollTimer = new wxTimer(this, SCROLL_TIMER_ID);
	scrollTimer->Stop();

	setHorizGrid( 1 );
	setHorizGridColor( 0, 0, (GLfloat) GRID_INTENSITY, (GLfloat) GRID_INTENSITY );
	disableHorizGrid();

	setVertGrid( 1 );
	setVertGridColor( 0, 0, (GLfloat) GRID_INTENSITY, (GLfloat) GRID_INTENSITY );
	disableVertGrid();

	glInitialized = false;
	deferPaint = false;
	panning = false;
	lastPanPaintMs = 0;

	minimap = NULL;
	
	canvasLocked = false;
}


klsGLCanvas::~klsGLCanvas() {
	scrollTimer->Stop();
	delete scrollTimer;
	return;
}

void klsGLCanvas::updateMiniMap() {
	GLPoint2f p1, p2;
	getViewport( p1, p2 );
	if (minimap != NULL) minimap->update(p1, p2);
}

// Setup the GL matrices for this canvas:
// (This needs to be called everytime that the matrices will be used.)
void klsGLCanvas::reclaimViewport( void ) {

	// Set the projection matrix:
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();

	wxSize sz = GetClientSize();
	// gluOrtho2D(left, right, bottom, top); (In world-space coords.)
	// Use logical coordinates for the projection matrix
	gluOrtho2D(panX, panX + (sz.GetWidth() * viewZoom), panY - (sz.GetHeight() * viewZoom), panY);

	// For glViewport, we need physical pixels on HiDPI/Retina displays
	// GetContentScaleFactor() returns 2.0 on Retina Macs, 1.0 elsewhere
	double scaleFactor = GetContentScaleFactor();
	glViewport(0, 0, (GLint)(sz.GetWidth() * scaleFactor), (GLint)(sz.GetHeight() * scaleFactor));

	// Set the model matrix:
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();
}


// Set the viewport (Set the left/top and right/bottom coordinates).
// NOTE: It will enforce a 1:1 aspect ratio, but it will make the best
// attempt to fit the zoom box as close as possible. Basically, it will
// fit the longest side to the window, and center the rest.
void klsGLCanvas::setViewport( GLPoint2f topLeft, GLPoint2f bottomRight ) {
	wxSize sz = GetClientSize();
	double sAspect = (double) sz.GetHeight() / (double) sz.GetWidth();

	double newWidth = bottomRight.x - topLeft.x;
	double newHeight = topLeft.y - bottomRight.y;
	double aspect = newHeight / newWidth;
	
	bool useWidth = aspect < sAspect; // Use the width as the limiting factor.
	
	double newZoom = 1.0;
	GLPoint2f newPan;
	
	if( useWidth ) {
		// The box width determines the new zoom factor:
		newZoom = newWidth / sz.GetWidth();

		// The x coordinate is the edge of the box:
		newPan.x = topLeft.x;

		// The y coordinate must center the box:
		newPan.y = topLeft.y + 0.5 * (sz.GetHeight() * newZoom - newHeight); // y + (1/2 of the leftover margins)
	} else {
		// The box height determines the new zoom factor:
		newZoom = newHeight / sz.GetHeight();

		// The y coordinate is the edge of the box:
		newPan.y = topLeft.y;

		// The x coordinate must center the box:
		newPan.x = topLeft.x - 0.5 * (sz.GetWidth() * newZoom - newWidth); // x - (1/2 of the leftover margins)
	}

	// Set the new viewport:
	setZoom( newZoom );
	setPan( newPan.x, newPan.y );
}


void klsGLCanvas::getViewport( GLPoint2f& p1, GLPoint2f& p2 ) {
	wxSize sz = GetClientSize();
	p1.x = panX;
	p1.y = panY;
	p2.x = panX + (sz.GetWidth()*viewZoom);
	p2.y = panY - (sz.GetHeight()*viewZoom);
}

GLPoint2f klsGLCanvas::mapToCanvas(wxPoint m) {
	int w, h;
	GetClientSize(&w, &h);

	float glX = panX + (m.x * viewZoom);
	float glY = panY - (m.y * viewZoom);

	return GLPoint2f(glX, glY);
}


void klsGLCanvas::wxOnPaint(wxPaintEvent& event) {
	wxPaintDC dc(this);
	wxGetApp().SetCurrentCanvas(this);
	// Init OpenGL once, but after SetCurrent
	if (!glInitialized)
	{
		glClearColor (1.0, 1.0, 1.0, 0.0);
		glColor3b(0, 0, 0);
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		
		//TODO: Check if alpha is hardware supported, and
		// don't enable it if not!
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		
		//*********************************
		//Edit by Joshua Lansford 4/05/07
		//I placed this in here to hopefully
		//anti-alis the the text font.
		//however, it doesn't but it does
		//anti-alies the gates which looks nice.
		//glEnable( GL_LINE_SMOOTH );
		//End of edit
		
		// Load the font texture
		guiText::loadFont(appConfig().appSettings.textFontFile);

		glInitialized = true;
	}

	reclaimViewport();
	renderSkiaLive();

	// Show the new buffer:
	glFlush();
	SwapBuffers();
}


void klsGLCanvas::wxOnEraseBackground(wxEraseEvent& WXUNUSED(event))
{
  // We're using double-buffering, so do nothing, to avoid flashing.
}


// The OS took the mouse capture away mid-drag (alt-tab, a popup, etc.). End any
// active drags so state doesn't get stuck; endDrag(BUTTON_MIDDLE) also flushes
// the final pan frame the throttle may have skipped.
void klsGLCanvas::wxOnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
	if (isDragging(BUTTON_LEFT))   endDrag(BUTTON_LEFT);
	if (isDragging(BUTTON_MIDDLE)) endDrag(BUTTON_MIDDLE);
	if (isDragging(BUTTON_RIGHT))  endDrag(BUTTON_RIGHT);
	// Base flags are cleared; let the subclass unwind its own drag state (a
	// half-placed new gate, an in-progress paste or move) which endDrag doesn't
	// touch -- otherwise it stays desynced from the now-cleared flags.
	cancelDrag();
}


void klsGLCanvas::wxOnSize(wxSizeEvent& event)
{
	wxGetApp().SetCurrentCanvas(this);
	Refresh();
}


void klsGLCanvas::getPan( GLdouble &x, GLdouble &y ) {
	x = this->panX;
	y = this->panY;
}


void klsGLCanvas::setPan( GLdouble newX, GLdouble newY ) {
	// Clamp the panning ranges:
	newX = max(newX, MIN_PAN);
	newX = min(newX, MAX_PAN);

	newY = max(newY, MIN_PAN);
	newY = min(newY, MAX_PAN);

	// Set the new pan values:
	panX = newX;
	panY = newY;

	// Reset the mouse coordinates to the new pan settings, and call OnMouseMove()
	// because the mouse's gl coords have changed. During a middle-drag pan we skip
	// OnMouseMove -- its collision/hover work is irrelevant to panning and would
	// run every mouse-move (twice, with the caller's own call), stuttering the
	// drag; setMouseCoords still runs so the next drag delta is computed correctly.
	setMouseCoords();
	if (!panning) {
		GLPoint2f m = getMouseCoords();
		OnMouseMove(m.x, m.y, isShiftDown, isControlDown);
	}
	updateMiniMap();

	// During a compound camera move (zoom = setZoom + setCenter, each of which
	// calls setPan) skip the repaint until the final state, so we don't flash the
	// intermediate frame.
	if (deferPaint) return;

	// While panning, throttle the synchronous repaint to ~frame rate. A real
	// mouse fires moves far faster than we can paint; without this each move
	// forces a full synchronous paint and they back up, so the view lags the
	// cursor. Intermediate moves still update panX/panY above, so no motion is
	// lost -- the next painted frame just uses the latest position. endDrag forces
	// a final paint so the last move always lands.
	if (panning) {
		wxLongLong now = wxGetLocalTimeMillis();
		if ((now - lastPanPaintMs).GetValue() < 12) return;
		lastPanPaintMs = now;
	}

	Refresh(); // Obviously it needs refreshed after a pan.
	// Force an immediate repaint rather than a deferred WM_PAINT. On Windows
	// WM_PAINT is the lowest-priority message, so during a drag it gets starved
	// by the flood of mouse-move events the canvas receives (the minimap stays
	// smooth only because it does not get those events). Painting synchronously
	// keeps interactive pan/zoom smooth.
	wxWindow::Update();
}


//Julian: Added to assist in zoom to mouse
void klsGLCanvas::setCenter(GLdouble newX, GLdouble newY)
{
	GLPoint2f topLeft;
	GLPoint2f bottomRight;
	GLPoint2f center;

	getViewport(topLeft, bottomRight);
	center = getCenter();

	setPan(newX - (center.x - topLeft.x), newY - (center.y - topLeft.y));
}

void klsGLCanvas::translatePan( GLdouble relX, GLdouble relY ) {
	GLdouble x, y;
	getPan(x, y);
	setPan( x + relX, y + relY );
}


void klsGLCanvas::OnScrollTimer(wxTimerEvent& event) {
	wxSize sz = GetClientSize();
	bool mouseOnBorder = false;
	bool isDuringDrag = isDragging(BUTTON_LEFT);

//TODO: Make this work so that you can auto-scroll even on the edge of a
// maximized window!
	wxPoint mPos = getMouseScreenCoords();
	if( (mPos.x == 0) || (mPos.x == sz.GetWidth()) ) {
		mouseOnBorder = true;
	}

	if( (mPos.y == 0) || (mPos.y == sz.GetHeight()) ) {
		mouseOnBorder = true;
	}

	//TODO: Find a way to disable auto-scroll when a new gate is being dragged around.
	if( isDuringDrag && (mouseOnBorder || mouseOutOfWindow) ) {
		GLdouble transX = 0.0;
		GLdouble transY = 0.0;
		if( mPos.x <= 0 ) {
			transX = -SCROLL_STEP * getZoom();
		} else if( mPos.x >= sz.GetWidth() ){
			transX = +SCROLL_STEP * getZoom();
		}
		
		if( mPos.y <= 0 ) {
			transY = +SCROLL_STEP * getZoom();
		} else if( mPos.y >= sz.GetHeight() ){
			transY = -SCROLL_STEP * getZoom();
		}

		// Call this once, to avoid the mouse callback being run twice:
		translatePan(transX, transY);
	}
}


void klsGLCanvas::setZoom( GLdouble newZoom ) {
	// Clamp the newZoom factor within the allowed zoom
	// sizes:
	newZoom = max(newZoom, MIN_ZOOM);
	newZoom = min(newZoom, MAX_ZOOM);

	GLPoint2f center = getCenter();
	GLPoint2f topLeft;
	GLPoint2f bottomRight;
	getViewport(topLeft, bottomRight);
	
	GLPoint2f oldDist = center - topLeft;
	GLPoint2f newDist = oldDist;

	oldDist.x *= newZoom / viewZoom;
	oldDist.y *= newZoom / viewZoom;

	viewZoom = newZoom;

	translatePan(newDist.x - oldDist.x, newDist.y - oldDist.y);
}


void klsGLCanvas::wxOnMouseEvent(wxMouseEvent& event) {
	reclaimViewport();
	
	isShiftDown = event.ShiftDown();
	isControlDown = event.ControlDown();
	
	// Always set the mouse coords to the current event:
	setMouseScreenCoords( event.GetPosition() );
	setMouseCoords();

	// Check all of the button events:
	if (event.LeftDown() ) {
		mouseOutOfWindow = false; // Assume that we clicked inside the window!
		beginDrag(BUTTON_LEFT);
		OnMouseDown(event); // Call the event handler.
	} else if( event.LeftUp() || event.LeftDClick()) {
		endDrag(BUTTON_LEFT);
		OnMouseUp( event );
	} else if( event.RightDown() || event.RightDClick() ) {
		beginDrag( BUTTON_RIGHT );
		OnMouseDown( event ); // Call the event handler.
	} else if( event.RightUp() ) {
		endDrag( BUTTON_RIGHT );
		OnMouseUp( event );
	} else if( event.MiddleDown() || event.MiddleDClick() ) {
		beginDrag( BUTTON_MIDDLE );
		OnMouseDown( event ); // Call the event handler.
	} else if( event.MiddleUp() ) {
		endDrag( BUTTON_MIDDLE );   // forces the final pan repaint
		OnMouseUp( event );
	} else {
		// It's not a button event, so check the others:
		if( event.Entering() ) {
			mouseOutOfWindow = false;
			scrollTimer->Stop();
			OnMouseEnter( event );
		} else if( event.Leaving() && !isDragging( BUTTON_MIDDLE ) ) { // Don't allow auto-scroll during pan-scrolling.
			// Flag the scroll event by telling it that the
			// mouse has left the window:
			mouseOutOfWindow = true;

			// Start the scroll timer:
			if( isAutoScrollOn() && isDragging( BUTTON_LEFT ) ) {
				scrollTimer->Start(SCROLL_TIMER_RATE);
			}

			// Call the event handler:
			OnMouseLeave( event );
		} else {
			// Handle the drag-pan event here if needed:
			if( isDragging( BUTTON_MIDDLE ) ) {
				GLPoint2f mouseDelta( getMouseCoords().x - getDragStartCoords( BUTTON_MIDDLE ).x,
										getMouseCoords().y - getDragStartCoords( BUTTON_MIDDLE ).y );

				// A pan only needs to move the camera + repaint. Flag it so setPan
				// skips the hover/collision OnMouseMove, and skip our own call too
				// -- that heavy per-move work is what makes the drag stutter.
				panning = true;
				translatePan( -mouseDelta.x, -mouseDelta.y );
				panning = false;
				// ...unless a left drag (gate move / rubber-band / new-gate) is also
				// in progress: it still needs OnMouseMove to track the cursor.
				if( isDragging( BUTTON_LEFT ) ) {
					GLPoint2f m = getMouseCoords();
					OnMouseMove(m.x, m.y, event.ShiftDown(), event.ControlDown());
				}
			} else {
				// It's nothing else, so it must be a mouse motion event:
				GLPoint2f m = getMouseCoords();
				OnMouseMove(m.x, m.y, event.ShiftDown(), event.ControlDown());
			}
		}
		
	}

// Refresh the canvas to show the mouse drag highlights if needed:
#ifdef CANVAS_DEBUG_TESTS_ON
	Refresh();
#endif

}


void klsGLCanvas::wxOnMouseWheel(wxMouseEvent& event) {
	reclaimViewport();

	// Accumulate mouse wheel events until they amount
	// to one "line", and then take them line at a time:
	wheelRotation += event.GetWheelRotation();
	int rotationLines = (int)wheelRotation / event.GetWheelDelta();
	wheelRotation -= rotationLines * event.GetWheelDelta();

	if (rotationLines != 0) {
		GLdouble panAmount = PAN_STEP * getZoom() * rotationLines;

#ifdef __WXOSX__
		// On macOS: Cmd + scroll/swipe = pan in scroll direction
		// Trackpad: two-finger swipe naturally pans in both directions with Cmd
		if (event.CmdDown()) {
			if (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL) {
				translatePan(panAmount, 0.0);
			} else {
				translatePan(0.0, panAmount);
			}
		} else if (event.ShiftDown()) {
			// Shift + scroll = horizontal pan (for mouse users)
			translatePan(panAmount, 0.0);
		} else if (event.ControlDown()) {
			// Ctrl + scroll = vertical pan (for mouse users)
			translatePan(0.0, panAmount);
		} else {
			// Default scroll = zoom
			OnMouseWheel(rotationLines / abs(rotationLines));
		}
#else
		// On Windows/Linux: Natural trackpad scrolling + modifier keys for mouse
		// Trackpad: two-finger horizontal swipe = horizontal pan, vertical swipe with Ctrl = vertical pan
		// Mouse: Shift + scroll = horizontal pan, Ctrl + scroll = vertical pan
		if (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL) {
			// Natural horizontal scrolling from trackpad
			translatePan(panAmount, 0.0);
		} else if (event.ShiftDown()) {
			// Shift + vertical scroll = horizontal pan (for mouse users)
			translatePan(panAmount, 0.0);
		} else if (event.ControlDown()) {
			// Ctrl + vertical scroll = vertical pan (trackpad or mouse)
			translatePan(0.0, panAmount);
		} else {
			// Default vertical scroll = zoom
			OnMouseWheel(rotationLines / abs(rotationLines));
		}
#endif
	}

	// Update the drag-pan event here if needed:
	if( isDragging( BUTTON_MIDDLE ) ) {
		GLPoint2f mouseDelta( getMouseCoords().x - getDragStartCoords( BUTTON_MIDDLE ).x,
								getMouseCoords().y - getDragStartCoords( BUTTON_MIDDLE ).y );

		translatePan( -mouseDelta.x, -mouseDelta.y );
	}

	updateMiniMap();
	event.Skip(); // Send the event on to wxOnMouseEvent, so that the gl coordinates get updated.
}


// Start a drag event right away, using the current mouse coordinates.
// This captures the mouse using CaptureMouse() and sets the "Drag Start Coords"
// to the current mouse coordinates.
// (This is usually called by this class right before OnMouseDown(), but
// can be called by the subclasses. For example, right after an OnMouseEnter()
// in which a gate is being dragged. Or, maybe also for a Paste from clipboard event.)
void klsGLCanvas::beginDrag( mouseButton whichButton ) {
	// If we are already in a drag event for this button, ignore any additional
	// ones that come along. This allows a beginDrag() called from
	// an event handler to not CaptureMouse() too many times.
	if( isDragging( whichButton ) ) return;
	
	// Set the keyboard focus to this window. This allows the user to re-set
	// the keyboard focus to this window by clicking on it.
	SetFocus();

	// Bind all mouse events to this window:
	if (!HasCapture()) {
		CaptureMouse();
	}
	
	// Set the dragging start coordinates:
	setDragStartCoords( getMouseCoords(), whichButton );
	
	// Set the flag to tell us that the button is dragging:
	setIsDragging( true, whichButton );
}


// Force the drag event to end, by unclaiming the mouse (If all other buttons haven't
// claimed a drag event too) and setting the "Drag End Coords".
void klsGLCanvas::endDrag( mouseButton whichButton ) {
	// Set the dragging start coordinates:
	setDragEndCoords( getMouseCoords(), whichButton );

	// Set the flag to tell us that the button is finished dragging:
	setIsDragging( false, whichButton );

	// Release the mouse capture, but only once no other button is still mid-drag:
	// the capture is shared, so a concurrent middle-pan + left gate-drag would
	// otherwise lose tracking when the first button is released.
	// (wxWidgets asserts on over-release; this is also called for double-click/ESC
	// where ReleaseMouse may run without a matching CaptureMouse -- HasCapture
	// guards that.)
	if (HasCapture() && !isDragging(BUTTON_LEFT) && !isDragging(BUTTON_MIDDLE)
	                 && !isDragging(BUTTON_RIGHT)) {
		ReleaseMouse();
	}

	// A middle-drag pan throttles its repaints, so its last frame may have been
	// skipped; paint the final position now, however the pan ended (MiddleUp, ESC,
	// or lost capture -- see wxOnCaptureLost).
	if (whichButton == BUTTON_MIDDLE) {
		Refresh();
		wxWindow::Update();
	}
}


void klsGLCanvas::wxKeyDown(wxKeyEvent& event) {
	wxGetApp().SetCurrentCanvas(this);
	reclaimViewport();

	// Give the subclassed handler first dibs on the event:
	OnKeyDown( event );

	// If the subclassed handler took the event, then don't handle it:
	if( event.GetSkipped() ) return;

	bool handled = true;
	switch (event.GetKeyCode()) {
	case WXK_LEFT:
	case WXK_NUMPAD_LEFT:
		translatePan(-PAN_STEP * getZoom(), 0.0);
		break;
	case WXK_RIGHT:
	case WXK_NUMPAD_RIGHT:
		translatePan(+PAN_STEP * getZoom(), 0.0);
		break;
	case WXK_UP:
	case WXK_NUMPAD_UP:
		translatePan(0.0, PAN_STEP * getZoom());
		break;
	case WXK_DOWN:
	case WXK_NUMPAD_DOWN:
		translatePan(0.0, -PAN_STEP * getZoom());
		break;
	case 43: // + key (Shift+=)
	case 61: // = key (for zoom in without shift on Mac)
	case WXK_NUMPAD_ADD:
		setZoom( getZoom() * ZOOM_STEP );
		break;
	case 45: // - key on top row (Works for both '-' and '_')
	case WXK_NUMPAD_SUBTRACT:
		setZoom( getZoom() / ZOOM_STEP );
		break;
	default:
		handled = false;
		break;
	}

	if (!handled) event.Skip();
	updateMiniMap();
}

void klsGLCanvas::wxKeyUp(wxKeyEvent& event) {
	reclaimViewport();

	OnKeyUp( event );
}

//Julian: Moved implementation from header
void klsGLCanvas::OnMouseWheel(long numOfLines) {
	zoomToMouse(numOfLines);
}

GLPoint2f klsGLCanvas::getSnappedPoint(GLPoint2f c) {
	GLfloat x = horizSpacing * floor(c.x / horizSpacing + 0.5);
	GLfloat y = vertSpacing * floor(c.y / vertSpacing + 0.5);
	return GLPoint2f(x, y);
}

void klsGLCanvas::setHorizGrid(GLfloat hSpacing) {
	horizOn = true;
	if (hSpacing != 0.0) horizSpacing = hSpacing;
}

void klsGLCanvas::setHorizGridColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
	hColor[0] = a;
	hColor[1] = b;
	hColor[2] = c;
	hColor[3] = d;
}

void klsGLCanvas::disableHorizGrid() {
	horizOn = false;
}

void klsGLCanvas::setVertGrid(GLfloat vSpacing) {
	vertOn = true;
	if (vSpacing != 0.0) vertSpacing = vSpacing;
}

void klsGLCanvas::setVertGridColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
	vColor[0] = a;
	vColor[1] = b;
	vColor[2] = c;
	vColor[3] = d;
}

void klsGLCanvas::disableVertGrid() {
	vertOn = false;
}

void klsGLCanvas::setMouseCoords() {
	setMouseCoords(mapToCanvas(getMouseScreenCoords()));
}

//Julian: Added to allow for zoom to mouse
void klsGLCanvas::zoomToMouse(long numLines)
{
	GLPoint2f center = getCenter();
	GLPoint2f mouse = getMouseCoords();

	GLPoint2f centerToMouse = mouse - center;
	centerToMouse.x /= getZoom();
	centerToMouse.y /= getZoom();

	// setZoom and setCenter both call setPan, which normally repaints
	// synchronously -- defer so the zoom paints once at the final camera state
	// instead of flashing the intermediate (center-fixed) frame before the
	// mouse-fixed correction.
	deferPaint = true;
	if (numLines > 0) {
		setZoom(getZoom() * (pow(ZOOM_STEP, numLines)));
	} else {
		setZoom(getZoom() / (pow(ZOOM_STEP, -numLines)));
	}

	centerToMouse.x *= getZoom();
	centerToMouse.y *= getZoom();

	setCenter(mouse.x - centerToMouse.x, mouse.y - centerToMouse.y);
	deferPaint = false;

	Refresh();
	wxWindow::Update();
}

GLPoint2f klsGLCanvas::getCenter() {
	GLPoint2f topLeft, bottomRight;
	getViewport(topLeft, bottomRight);

	GLPoint2f center = bottomRight + topLeft;
	center.x /= 2;
	center.y /= 2;

	return center;
}
