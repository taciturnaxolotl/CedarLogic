/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   gateImage: Generates a bitmap for a gate in the library, used in palette
*****************************************************************************/

#include "gateImage.h"
#include "PaletteDrag.h"
#include "RenderMode.h"
#include "Settings.h"
#include "GateLibrary.h"
#include "wx/image.h"
#include "wx/wx.h"
#include "klsGLCanvas.h"
#include "guiText.h"
#include <fstream>
#include <wx/dnd.h>
#include "render/SkiaProbe.h"
#include "render/Scene.h"
#include "render/RenderStyle.h"
#include "MainFrame.h"

BEGIN_EVENT_TABLE(gateImage, wxWindow)
    EVT_PAINT(gateImage::OnPaint)
    EVT_ENTER_WINDOW( gateImage::OnEnterWindow )
    EVT_LEAVE_WINDOW( gateImage::OnLeaveWindow )
    EVT_MOUSE_EVENTS( gateImage::mouseCallback )
	EVT_ERASE_BACKGROUND(gateImage::OnEraseBackground)
END_EVENT_TABLE()

DECLARE_APP(MainApp)

gateImage::gateImage( string gateName, wxWindow *parent, wxWindowID id,
        const wxPoint& pos,
        const wxSize& size,
        long style, const wxString& name ) : 
        wxWindow(parent, id, pos, size, style|wxFULL_REPAINT_ON_RESIZE, name ) {
	m_init = false;
	inImage = false;

	m_gate = GUICircuit().createGate(gateName, 0, true);
	if (m_gate == NULL) return;
	m_gate->setGLcoords(0,0);
	m_gate->calcBBox();
	this->gateName = gateName;
	update();

	delete m_gate;
	SetToolTip(gateLibrary().libraries[gateLibrary().gateNameToLibrary[gateName]][gateName].caption);
}

gateImage::~gateImage() {
}

void gateImage::OnPaint(wxPaintEvent &event) {
	wxPaintDC dc(this);
	wxBitmap gatebitmap(gImage);
	dc.DrawBitmap(gatebitmap, 0, 0, true);	
	if (inImage) {
		dc.SetPen(wxPen(*wxBLUE, 2, wxPENSTYLE_SOLID));
	} else {
		dc.SetPen(wxPen(*wxWHITE, 2, wxPENSTYLE_SOLID));
	}
		dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
		dc.DrawRectangle(0,0,IMAGESIZE,IMAGESIZE);
	//event.Skip();
}

void gateImage::mouseCallback( wxMouseEvent& event) {
	if (event.LeftDown()) {
#ifndef USE_WX_DRAGDROP
		paletteDrag().newGateToDrag = gateName;

#if defined(__WXGTK__) || defined(__WXOSX__)
		// On GTK and macOS the canvas doesn't get mouse events when dragging over
		// it, so we have to tell it to capture the mouse.
		wxGetApp().mainframe->PreGateDrag();

		// Stop drawing the border
		inImage = false;
		Refresh();
#endif

#else
		// If USE_WX_DRAGDROP is defined, we use the wxwidgets native
		// drag and drop. This seems more likely to work cross platform,
		// but it doesn't provide a preview of the gate being dragged.
		wxTextDataObject textData(gateName);
		wxDropSource source(textData, this);

		int flags = 0;
		source.DoDragDrop(flags);
#endif
	} else if (event.LeftUp()) {
		paletteDrag().newGateToDrag = "";
	} else {
		// This is required to pass scroll events through
		event.Skip();
	}
}

void gateImage::OnEraseBackground( wxEraseEvent& event ) {
	// Do nothing, so that the palette doesn't flicker!
}

// Fit the gate's model box into a `size`-square thumbnail: 0.5 world units of
// padding, then letterboxed on the limiting axis.
cl::render::Transform gateImage::thumbnailTransform(int size) const {
	klsBBox box = m_gate->getModelBBox();
	// minCorner is (left, top), maxCorner is (right, bottom), so y decreases from
	// min to max.
	GLPoint2f minCorner(box.getLeft() - 0.5f, box.getTop() + 0.5f);
	GLPoint2f maxCorner(box.getRight() + 0.5f, box.getBottom() - 0.5f);

	double mapWidth = maxCorner.x - minCorner.x;
	double mapHeight = minCorner.y - maxCorner.y;
	if (mapWidth <= 0.0) mapWidth = 1.0;
	if (mapHeight <= 0.0) mapHeight = 1.0;

	// Square thumbnail, so the aspect is 1: pad the shorter axis.
	double left = minCorner.x, right = maxCorner.x;
	double top = minCorner.y, bottom = maxCorner.y;
	if (mapWidth >= mapHeight) {
		double pad = 0.5 * (mapWidth - mapHeight);
		top += pad;
		bottom -= pad;
	} else {
		double pad = 0.5 * (mapHeight - mapWidth);
		left -= pad;
		right += pad;
	}

	cl::render::Transform t;
	t.a = (float)(size / (right - left));
	t.c = 0.0f;
	t.e = (float)(-left * size / (right - left));
	t.b = 0.0f;
	t.d = (float)(-size / (top - bottom));   // world y up -> device y down
	t.f = (float)(top * size / (top - bottom));
	return t;
}

// Render the thumbnail through Skia into an offscreen raster surface. Skia
// anti-aliases natively, so no supersampling is needed for smooth curves.
bool gateImage::generateImageSkia() {
	if (m_gate == NULL) return false;
	const int size = GATEIMAGESIZE;
	wxImage img(size, size);

	guiGate *gate = m_gate;
	cl::render::Transform t = thumbnailTransform(size);
	const cl::render::RenderStyle style = cl::render::RenderStyle::print();
	if (!cl::render::skiaRenderToRGB(size, size,
			[gate, &t, &style](cl::render::Scene &scene) {
				scene.setViewport(t);
				gate->drawToScene(scene, style);
			},
			img.GetData())) {
		return false;
	}
	gImage = img;
	return true;
}

void gateImage::update() {
	generateImageSkia();
}
