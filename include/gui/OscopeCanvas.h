/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   OscopeCanvas: renders the waveform for the oscope
*****************************************************************************/

#ifndef OSCOPECANVAS_H_
#define OSCOPECANVAS_H_

class GUICircuit;
class OscopeCanvas;

#include "MainApp.h"
#include "wx/glcanvas.h"
#include "GUICircuit.h"
#include "logic_values.h"

#include <map>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <deque>
using namespace std;

class guiGate;

#define OSCOPE_HORIZONTAL 200.0

class OscopeFrame;

// Engine-neutral rendering seam (Workstream G); defined in gui/render/.
namespace cl { namespace render { class Scene; struct Transform; } }

class OscopeCanvas: public wxGLCanvas
{
public:
    OscopeCanvas( wxWindow *parent, GUICircuit* gCircuit, wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0, const wxString& name = "OscopeCanvas" );

    virtual ~OscopeCanvas();

    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    // Fixed-rate repaint. UpdateData() (called once per interim sim step) only
    // marks the data dirty; this timer does the actual Refresh at ~60fps. That
    // decouples the render rate from the sim's data rate, so a fast sim can't
    // flood the shared GL context with oscope repaints (which lagged the whole UI).
    void OnRenderTimer(wxTimerEvent& event);

    void UpdateMenu(void);
	void UpdateData(void);
		
#ifdef WITH_SKIA
    // G3: render the waveforms through Skia's Ganesh backend instead of GL.
    bool OnRenderSkia();
    void drawOscopeScene(cl::render::Scene& scene, const cl::render::Transform& t,
                         unsigned int numberOfWires);
#endif
    wxImage generateImage();
    
    void clearData( void ) {
    	stateValues.clear();
    };

	// Pointer to the main application graphic circuit
	GUICircuit* gCircuit;

private:
	// Stored values of wire states:
	map< string, deque< StateType > > stateValues;

	// Cache of TO-gate (feed source) lookups by JUNCTION_ID, so UpdateData()
	// doesn't re-walk the whole gate list on every interim step. Rebuilt only
	// when the gate set changes (tracked by count) or a structural change forces
	// it via UpdateMenu(); during a running sim the gates are static, so the
	// per-step cost drops from O(gates) to O(feeds).
	unordered_map< string, guiGate* > toGateCache;
	size_t toGateCacheGateCount;

	// Fixed-rate repaint (see OnRenderTimer): dataDirty is set by UpdateData and
	// consumed by the timer, which repaints only when something actually changed.
	wxTimer* renderTimer;
	bool dataDirty;
	std::chrono::steady_clock::time_point lastPaintTime;

	bool m_init;
	
	OscopeFrame* parentFrame;
	
	DECLARE_EVENT_TABLE()
};

#endif /*OSCOPECANVAS_H_*/
