/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   OscopeCanvas: renders the waveform for the oscope
*****************************************************************************/

#include "OscopeCanvas.h"
#include "MainApp.h"
#include "GUICanvas.h"
#include "OscopeFrame.h"
#include "guiWire.h"

#include "render/Scene.h"
#include "render/RenderStyle.h"
#ifdef WITH_SKIA
#include "render/SkiaProbe.h"
#endif

// Included to use the min() and max() templates:
#include <algorithm>
#include <vector>
using namespace std;

DECLARE_APP(MainApp)

#define ID_OSCOPE_RENDER_TIMER 7790
#define OSCOPE_RENDER_INTERVAL_MS 16   // ~60fps

BEGIN_EVENT_TABLE(OscopeCanvas, wxGLCanvas)
    EVT_PAINT(OscopeCanvas::OnPaint)
    EVT_ERASE_BACKGROUND(OscopeCanvas::OnEraseBackground)
    EVT_TIMER(ID_OSCOPE_RENDER_TIMER, OscopeCanvas::OnRenderTimer)
END_EVENT_TABLE()

OscopeCanvas::OscopeCanvas(wxWindow *parent, GUICircuit* gCircuit, wxWindowID id,
	const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxGLCanvas( parent, glCanvasAttributes(), id, pos, size, style|wxFULL_REPAINT_ON_RESIZE|wxSUNKEN_BORDER ) {

	this->gCircuit = gCircuit;
	parentFrame = (OscopeFrame*) parent;
	toGateCacheGateCount = (size_t)-1;   // force a rebuild on first UpdateData
	dataDirty = false;
	renderTimer = new wxTimer(this, ID_OSCOPE_RENDER_TIMER);
	renderTimer->Start(OSCOPE_RENDER_INTERVAL_MS);
}

OscopeCanvas::~OscopeCanvas(){
	if (renderTimer) { renderTimer->Stop(); delete renderTimer; renderTimer = NULL; }
	return;
}

// Fixed-rate repaint: coalesce all the data updates since the last tick into one
// Refresh, and only when the panel is actually on screen.
void OscopeCanvas::OnRenderTimer(wxTimerEvent& WXUNUSED(event)) {
	if (dataDirty) {
		dataDirty = false;
		lastPaintTime = std::chrono::steady_clock::now();
		if (IsShownOnScreen()) Refresh();
	}
}

#ifdef WITH_SKIA
// Render the waveforms through Skia. The world box is
// (0, -0.25) .. (OSCOPE_HORIZONTAL, numberOfWires*1.5) -- note it is y-DOWN
// (top = -0.25), unlike the main canvas -- so the world->device transform has a
// positive y scale and no flip.
bool OscopeCanvas::OnRenderSkia() {
	using namespace cl::render;
	const unsigned int numberOfWires = parentFrame->numberOfFeeds();
	wxSize sz = GetClientSize();
	const double sf = GetContentScaleFactor();
	const int w = (int)(sz.GetWidth() * sf), h = (int)(sz.GetHeight() * sf);
	if (w <= 0 || h <= 0) return false;
	const float worldH = numberOfWires * 1.5f + 0.25f;  // top -0.25 .. bottom nw*1.5
	if (worldH <= 1e-3f) return false;
	const float scaleX = (float)w / (float)OSCOPE_HORIZONTAL;
	const float scaleY = (float)h / worldH;
	Transform t;
	t.a = scaleX; t.c = 0; t.e = 0;
	t.b = 0; t.d = scaleY; t.f = 0.25f * scaleY;   // world y=-0.25 -> device 0
	OscopeCanvas* self = this;
	return skiaRenderWindow(w, h, 0, [self, &t, numberOfWires](Scene& scene) {
		self->drawOscopeScene(scene, t, numberOfWires);
	});
}

void OscopeCanvas::drawOscopeScene(cl::render::Scene& scene,
                                   const cl::render::Transform& t,
                                   unsigned int numberOfWires) {
	using namespace cl::render;
	scene.setViewport(t);
	const Stroke gridStroke(Color(0.0f, 0.0f, (float)GRID_INTENSITY,
	                              (float)GRID_INTENSITY), 1.0f);

	// Ten vertical division lines (x = 0 and OSCOPE_HORIZONTAL/10 * 1..9).
	std::vector<Point> vlines;
	for (int k = 0; k < 10; k++) {
		const float x = (float)(OSCOPE_HORIZONTAL / 10.0) * k;
		vlines.push_back(Point(x, -0.5f));
		vlines.push_back(Point(x, numberOfWires * 1.5f));
	}
	if (!vlines.empty()) scene.lines(&vlines[0], vlines.size(), gridStroke);

	// Batch every waveform segment by its state colour and draw the whole scope in
	// a handful of scene.lines() calls. The old code issued a scene.lines() call
	// AND allocated a std::vector for every single data point (up to
	// OSCOPE_HORIZONTAL per wire, every frame) -- that per-point churn is what made
	// a running scope lag. Line states share three colours, so three vectors cover
	// them; the rarer solid states (UNKNOWN/CONFLICT) stay per-point fills.
	std::vector<Point> segZero, segOne, segHiZ;  // ZERO=black, ONE=red, HI_Z=green
	const size_t reserveHint = (size_t)(numberOfWires * OSCOPE_HORIZONTAL);
	segZero.reserve(reserveHint); segOne.reserve(reserveHint);

	for (unsigned int i = 0; i < numberOfWires; i++) {
		const unsigned int wireNum = i;
		// Horizontal baseline for this wire.
		const Point hb[2] = { Point(0.0f, wireNum * 1.5f + 1.0f),
		                      Point((float)OSCOPE_HORIZONTAL, wireNum * 1.5f + 1.0f) };
		scene.lines(hb, 2, gridStroke);

		map< string, deque< StateType > >::iterator thisWire =
			stateValues.find(parentFrame->getFeedName(i).c_str());
		if (thisWire == stateValues.end()) continue;

		deque< StateType >::reverse_iterator wireVal = (thisWire->second).rbegin();
		float horizLoc = (float)OSCOPE_HORIZONTAL, y = 0.0f, lastY = 0.0f;
		bool firstTime = true;
		while (wireVal != (thisWire->second).rend()) {
			std::vector<Point>* seg = nullptr;
			Color solidColor(0, 0, 0, 1);
			bool solid = false;
			switch (*wireVal) {
				case ZERO:     seg = &segZero; y = 1.0f + wireNum * 1.5f; break;
				case ONE:      seg = &segOne;  y = 0.0f + wireNum * 1.5f; break;
				case HI_Z:     seg = &segHiZ;  y = 0.5f + wireNum * 1.5f; break;
				case UNKNOWN:  solidColor = Color(0.3f, 0.3f, 1.0f); y = 0.75f + wireNum * 1.5f; solid = true; break;
				case CONFLICT: solidColor = Color(0, 1, 1);          y = 0.75f + wireNum * 1.5f; solid = true; break;
				default: break;
			}
			if (solid) {
				scene.fillRect(Point(horizLoc - 1.0f, 0.0f + wireNum * 1.5f),
				               Point(horizLoc, y), solidColor);
			} else if (seg) {
				if (!firstTime && lastY != y) {   // rising/falling edge
					seg->push_back(Point(horizLoc, lastY));
					seg->push_back(Point(horizLoc, y));
				}
				seg->push_back(Point(horizLoc, y));          // the run
				seg->push_back(Point(horizLoc - 1.0f, y));
			}
			firstTime = false;
			horizLoc -= 1.0f;
			lastY = y;
			++wireVal;
		}
	}

	if (!segZero.empty()) scene.lines(&segZero[0], segZero.size(), Stroke(Color(0, 0, 0), 1.0f));
	if (!segOne.empty())  scene.lines(&segOne[0],  segOne.size(),  Stroke(Color(1, 0, 0), 1.0f));
	if (!segHiZ.empty())  scene.lines(&segHiZ[0],  segHiZ.size(),  Stroke(Color(0, 0.78f, 0), 1.0f));
}
#endif  // WITH_SKIA

void OscopeCanvas::OnPaint(wxPaintEvent& event){
	wxPaintDC dc(this);
	wxGetApp().SetCurrentCanvas(this);
	// Skia sets its own GL state; nothing to initialise here.
	OnRenderSkia();

	// Show the new buffer:
	glFlush();
	SwapBuffers();
}

void OscopeCanvas::UpdateData(void){
	//Declaration of variables
	deque<StateType> temp;

	unordered_map< unsigned long, guiGate* >* gateList = gCircuit->getGates();

	// Rebuild the JUNCTION_ID -> TO gate lookup only when the gate set changes.
	// This is called once per interim (logic) step, and a fast sim emits many per
	// GUI drain; re-walking the whole gate list each time (the old behaviour) made
	// the oscope -- and, because it blocks the GUI thread, the whole tick rate --
	// lag. During a running sim the gates are static, so the cache holds.
	if (parentFrame->numberOfFeeds() > 0 && gateList->size() != toGateCacheGateCount) {
		toGateCache.clear();
		for (auto& g : *gateList) {
			if (g.second->getGUIType() == "TO")
				toGateCache[g.second->getLogicParam("JUNCTION_ID")] = g.second;
		}
		toGateCacheGateCount = gateList->size();
	}

	set< string > liveTOs;

	//Check to see if wire has already been added to OSCOPE
	map< string, bool > hasBeenAdded;

	for (unsigned int i = 0; i < parentFrame->numberOfFeeds(); i++) {
		string junctionName = parentFrame->getFeedName(i).c_str();
		if (junctionName == NONE_STR || junctionName == "") continue;

		if(hasBeenAdded.find(junctionName) == hasBeenAdded.end()) {
			hasBeenAdded[junctionName] = true;
			// Keep track of all junction names that are still valid.
			// If a gate disappears or changes junction names, then
			// we want to remove it from our data structure.
			liveTOs.insert(junctionName);

			// Create a new storage space for its data if we need it:
			if( stateValues.find(junctionName) == stateValues.end() ) {
				stateValues[junctionName] = temp;
			}

			// Look up the TO gate feeding this junction (O(1) via the cache).
			guiGate* currentGate = NULL;
			{
				auto it = toGateCache.find(junctionName);
				if (it != toGateCache.end()) currentGate = it->second;
			}
			if (currentGate == NULL) { // Just in case of error
				stateValues.erase(junctionName);
				parentFrame->cancelFeed(i);
				//parentFrame->comboBoxVector[i]->SetValue("[None]");
				continue;
			}
			
			map<string, GLPoint2f> hsList = currentGate->getHotspotList();
			if( hsList.size() != 0 ) {
				string firstInput = (hsList.begin())->first;

				// Get the wire connected to the TO's input:
				if( currentGate->isConnected(firstInput) ) {
					guiWire* myWire = currentGate->getConnection( firstInput );
					
					// Push the current state onto this TO's data queue:
					stateValues[junctionName].push_back(myWire->getState()[0]);
				} else {
					// The TO is not connected, so the state is UNKNOWN:
					stateValues[junctionName].push_back(UNKNOWN); 
				}
			}
			
			// If the data queue is too big, then pop data off the other
			// end of the queue to make it the right size:
			if(stateValues[junctionName].size() > OSCOPE_HORIZONTAL) stateValues[junctionName].pop_front();
		}
	} // for ( not end of list )
	
	
	// Clear out data queues for TOs that don't exist anymore:
	map< string, deque< StateType > >::iterator checkData = stateValues.begin();
	while( checkData != stateValues.end() ) {
		if( liveTOs.find( checkData->first ) == liveTOs.end() ) {
			stateValues.erase( checkData );
			checkData = stateValues.begin();
		}
		else {
			checkData++;
		}
	}
	
	// The trace advances one sample per step, so it looks even only if it is drawn
	// once per sample. Repaint synchronously here when the samples are slower than
	// the frame cap: deferring to a fixed-rate timer would re-quantise a 25ms
	// sample onto the timer's grid (16/32/16ms...), which is visible as jitter.
	// Faster-than-60fps sample rates fall back to the timer, which coalesces them.
	auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPaintTime).count() >= 15) {
		lastPaintTime = now;
		dataDirty = false;
		if (IsShownOnScreen()) { Refresh(false); Update(); }
	} else {
		dataDirty = true;
	}
}



void OscopeCanvas::OnEraseBackground(wxEraseEvent& WXUNUSED(event))
{
  // Do nothing, to avoid flashing.
}

void OscopeCanvas::UpdateMenu()
{
	// The gate structure may have changed (add/remove/renamed junction), so drop
	// the cached TO-gate lookup; UpdateData() rebuilds it on the next call.
	toGateCacheGateCount = (size_t)-1;

	//*******************************
	//Edit by Joshua Lansford 3/11/07
	//This edit is to retrofit this
	//method so that all it does is genrate
	//a list of possable feeds and then
	//passes it back up to its parent.
	//This way it does not mess directly
	//with the values in the combo boxes
	//which are some reason not remembering
	//what value they should be currently holding
	//The edit ends with the end of this function
	
	unordered_map< unsigned long, guiGate* >* gateList = gCircuit->getGates();
	
	vector< string > namesOfPossableFeeds;
	
	map< string, bool > alreadyAdded;
	
	//iterate over all gates
	for( unordered_map< unsigned long, guiGate* >::iterator 
	       gateIterator = gateList->begin(); 
	       gateIterator != gateList->end(); 
	       gateIterator++ ){
	   guiGate* aGate = gateIterator->second;
	   //select out the gates which are TOs
	   if( aGate->getGUIType() == "TO" ){
	   		string feedName;        
	   		feedName = aGate->getLogicParam("JUNCTION_ID");
	   		
	   		//check if it has already been added
	   		if( alreadyAdded.find( feedName ) == alreadyAdded.end() ){
	   			
	   			//add name to list
	   			namesOfPossableFeeds.push_back( feedName );
	   			alreadyAdded[ feedName ] = true;
	   		}
	   }
	}
	
	parentFrame->updatePossableFeeds( &namesOfPossableFeeds );
	
	/*
	
	//Sets variables
	unordered_map< unsigned long, guiGate* >* gateList = gCircuit->getGates();
	unordered_map< unsigned long, guiGate* >::iterator theGate = gateList->begin();
	
	//Sets size
	//unsigned int size = (parentFrame->comboBoxVector).size();
	unsigned int size = parentFrame->numberOfFeeds();
	
	for(unsigned int x = 0; x < size; x++)
	{
		//wxString oldVal = (parentFrame->comboBoxVector[x])->GetValue();
		string oldVal = parentFrame->getFeedName( x );
		//Update Combo Box Data
		(parentFrame->comboBoxVector[x])->Clear();
	
		//starts new array of strings
		wxArrayString strings;

		theGate = gateList->begin();

		//Adds names to dialog box
		while (theGate != gateList->end())
		{	
			//Tests Gate ID
			if((theGate->second)->getGUIType() == "TO" )
			{
				//Gets gate ID
				string junctionName = (theGate->second)->getLogicParam("JUNCTION_ID");
			
				(parentFrame->comboBoxVector[x])->Append(junctionName.c_str());
			}
		
			theGate++;
		}
		(parentFrame->comboBoxVector[x])->Append("[None]");
		(parentFrame->comboBoxVector[x])->Append("[Remove]");
		
		// *******************************************
		//Edit by Joshua Lansford 2/22/07
		//FindString is insensitive.  Therefore just
		//because it finds something doesn't mean that
		//our old value  is still valid.  Therefore
		//we must search manually
		
		//if ((parentFrame->comboBoxVector[x])->FindString(oldVal) != -1 ) {
		
		bool foundIt = false;
		for( int search = 0; 
		     search < (parentFrame->comboBoxVector[x])->GetCount() && !foundIt;
		     ++search ){
			if( oldVal == (parentFrame->comboBoxVector[x])->GetString( search ) ){
				foundIt = true;
			}
		}
		if( foundIt ){
		//End of Edit************************************************
		
		
			(parentFrame->comboBoxVector[x])->SetValue(oldVal);
		} else {
			(parentFrame->comboBoxVector[x])->SetValue("[None]");
		}
	}
	*/
}

// Print the canvas contents to a bitmap, via an offscreen Skia surface. Uses
// logical (unscaled) pixels so the exported image matches the panel's size, and
// the same world->device transform OnRenderSkia builds.
wxImage OscopeCanvas::generateImage(){
	using namespace cl::render;
	wxSize sz = GetClientSize();
	const int w = sz.GetWidth(), h = sz.GetHeight();
	if (w <= 0 || h <= 0) return wxImage();

	// The caller wraps this in a wxBitmap, so always hand back a valid image --
	// blank white if the render can't run.
	wxImage img(w, h);
	memset(img.GetData(), 0xFF, (size_t)w * h * 3);

	const unsigned int numberOfWires = parentFrame->numberOfFeeds();
	const float worldH = numberOfWires * 1.5f + 0.25f;  // top -0.25 .. bottom nw*1.5
	if (worldH <= 1e-3f) return img;

	Transform t;
	t.a = (float)w / (float)OSCOPE_HORIZONTAL; t.c = 0; t.e = 0;
	t.b = 0; t.d = (float)h / worldH; t.f = 0.25f * ((float)h / worldH);

	OscopeCanvas* self = this;
	skiaRenderToRGB(w, h, [self, &t, numberOfWires](Scene& scene) {
			self->drawOscopeScene(scene, t, numberOfWires);
		}, img.GetData());
	return img;
}
