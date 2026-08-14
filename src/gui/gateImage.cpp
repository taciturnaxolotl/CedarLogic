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
#include <fstream>
#include <wx/dnd.h>
#include "render/SkiaProbe.h"
#include "render/Scene.h"
#include "render/RenderStyle.h"
#include "MainFrame.h"

BEGIN_EVENT_TABLE(gateImage, wxWindow)
    EVT_PAINT(gateImage::OnPaint)
    EVT_SIZE(gateImage::OnSize)
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
	renderedPx = 0;

	this->gateName = gateName;
	update();
	SetToolTip(gateLibrary().libraries[gateLibrary().gateNameToLibrary[gateName]][gateName].caption);
}

gateImage::~gateImage() {
}

void gateImage::OnPaint(wxPaintEvent &event) {
	wxPaintDC dc(this);
	// Sections other than the one on screen are never laid out, so their tiles
	// have nothing drawn yet; the first paint after being shown is where they
	// get it. (Rendering all ~420 of them up front just to throw most away is
	// what this avoids.)
	if (!gBitmap.IsOk()) update();
	// Centre the art in the tile. The picture is square and the tile need not be
	// (the palette's grid hands its rows any spare height), so drawing at the
	// origin would pile all of that slack against the bottom edge.
	if (gBitmap.IsOk()) {
		const wxSize tile = GetClientSize();
		dc.DrawBitmap(gBitmap,
		              (tile.x - gBitmap.GetLogicalWidth()) / 2,
		              (tile.y - gBitmap.GetLogicalHeight()) / 2, true);
	}
	if (inImage) {
		dc.SetPen(wxPen(*wxBLUE, 2, wxPENSTYLE_SOLID));
	} else {
		dc.SetPen(wxPen(*wxWHITE, 2, wxPENSTYLE_SOLID));
	}
	dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
	const wxSize sz = GetClientSize();   // the tile is sized by the palette, not fixed
	dc.DrawRectangle(0, 0, sz.x, sz.y);
}

// The tile's size is chosen by the palette from the panel width, so the art has
// to be re-rendered to match. Skia draws it from vectors, so a bigger tile is
// genuinely bigger art rather than an upscale.
void gateImage::OnSize(wxSizeEvent &event) {
	update();
	Refresh();
	event.Skip();
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
cl::render::Transform gateImage::thumbnailTransform(guiGate* gate, int size) const {
	klsBBox box = gate->getModelDrawBBox();
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
	// Build the gate, draw it, drop it. The palette holds one of these tiles per
	// library entry, and keeping a live guiGate in each -- for something only
	// ever used to draw a picture -- is a lot of circuit to carry around.
	guiGate *gate = GUICircuit().createGate(gateName, 0, true);
	if (gate == NULL) return false;
	gate->setGLcoords(0, 0);
	gate->calcBBox();

	// Render at device pixels, not logical ones: the tile is drawn on whatever
	// display the window is on, and rasterising at logical size means the OS
	// upscales it on a Retina screen. The bitmap carries the scale so it still
	// occupies `side` logical pixels.
	const wxSize client = GetClientSize();
	if (client.x <= 0 || client.y <= 0) return false;   // not laid out yet
	const double scale = GetContentScaleFactor();
	const int side = wxMax(8, wxMin(client.x, client.y) - 2);
	const int px = wxMax(8, (int)(side * scale + 0.5));
	// Layout settles over several size events (the panel columns, then the
	// scrollbar appearing and narrowing them). Only the last one changes the
	// picture, so redraw on a genuine change of size and not on each pass.
	if (px == renderedPx) return true;
	renderedPx = px;

	wxImage img(px, px);
	cl::render::Transform t = thumbnailTransform(gate, px);
	const cl::render::RenderStyle style = cl::render::RenderStyle::print();
	const bool ok = cl::render::skiaRenderToRGB(px, px,
			[gate, &t, &style](cl::render::Scene &scene) {
				scene.setViewport(t);
				gate->drawToScene(scene, style);
			},
			img.GetData());
	delete gate;
	if (!ok) return false;

	gImage = img;
	gBitmap = wxBitmap(img, -1, scale);
	return true;
}

void gateImage::update() {
	generateImageSkia();
}
