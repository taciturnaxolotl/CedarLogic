/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                    Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   OscopeFrame: Docked panel for the Oscope
*****************************************************************************/

#include "MainApp.h"
#include "OscopeFrame.h"
#include "wx/filedlg.h"
#include "wx/menu.h"
#include "wx/settings.h"
#include "GUICircuit.h"
#include "wx/clipbrd.h"
#include "wx/artprov.h"
#include <fstream>
#include <iomanip>

#ifdef __APPLE__
#include "NativeIcons.h"
#endif

DECLARE_APP(MainApp)

OscopeFrame::OscopeFrame(wxWindow *parent, GUICircuit* gCircuit)
       : wxPanel(parent, wxID_ANY)
{
	this->gCircuit = gCircuit;
	paused = false;

	oSizer = new wxBoxSizer( wxVERTICAL );

	// Create the toolbar
	oscopeToolBar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);

#ifdef __WXOSX__
	auto sfSymbol = [](const char* name) -> wxBitmap {
		wxBitmap bmp = NativeIcon_GetSFSymbol(name, 15);
		if (bmp.IsOk()) return bmp;
		return wxArtProvider::GetBitmap(wxART_QUESTION, wxART_TOOLBAR);
	};

	oscopeToolBar->AddTool(ID_OSCOPE_PAUSE, "Pause", sfSymbol("pause.fill"), "Pause/Reset", wxITEM_CHECK);
	oscopeToolBar->AddSeparator();
	oscopeToolBar->AddTool(ID_OSCOPE_ADD, "Add Signal", sfSymbol("plus"), "Add signal");
	oscopeToolBar->AddTool(ID_OSCOPE_REMOVE, "Remove Signal", sfSymbol("minus"), "Remove selected signal");
	oscopeToolBar->AddSeparator();
	oscopeToolBar->AddTool(ID_OSCOPE_EXPORT, "Export", sfSymbol("doc.on.clipboard"), "Export to clipboard");
	oscopeToolBar->AddTool(ID_OSCOPE_LOAD, "Load", sfSymbol("folder"), "Load layout");
	oscopeToolBar->AddTool(ID_OSCOPE_SAVE, "Save", sfSymbol("square.and.arrow.down"), "Save layout");
#else
	oscopeToolBar->AddTool(ID_OSCOPE_PAUSE, "Pause", wxArtProvider::GetBitmap(wxART_CROSS_MARK, wxART_TOOLBAR), "Pause/Reset", wxITEM_CHECK);
	oscopeToolBar->AddSeparator();
	oscopeToolBar->AddTool(ID_OSCOPE_ADD, "Add Signal", wxArtProvider::GetBitmap(wxART_PLUS, wxART_TOOLBAR), "Add signal");
	oscopeToolBar->AddTool(ID_OSCOPE_REMOVE, "Remove Signal", wxArtProvider::GetBitmap(wxART_MINUS, wxART_TOOLBAR), "Remove selected signal");
	oscopeToolBar->AddSeparator();
	oscopeToolBar->AddTool(ID_OSCOPE_EXPORT, "Export", wxArtProvider::GetBitmap(wxART_COPY, wxART_TOOLBAR), "Export to clipboard");
	oscopeToolBar->AddTool(ID_OSCOPE_LOAD, "Load", wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_TOOLBAR), "Load layout");
	oscopeToolBar->AddTool(ID_OSCOPE_SAVE, "Save", wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_TOOLBAR), "Save layout");
#endif

	oscopeToolBar->Realize();
#ifdef __WXOSX__
	// Set up both normal and alternate (checked) SF Symbol images on the
	// native NSButton so macOS handles the toggle automatically.
	NativeIcon_ConfigureEmbeddedToggleTool(oscopeToolBar, ID_OSCOPE_PAUSE,
		"pause.fill", "arrow.trianglehead.counterclockwise", 15);
#endif
	oSizer->Add(oscopeToolBar, wxSizerFlags(0).Expand());

	// Create horizontal sizer for signal list + canvas
	wxBoxSizer* contentSizer = new wxBoxSizer( wxHORIZONTAL );

	signalList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(120, -1), 0, nullptr, wxLB_SINGLE);
	contentSizer->Add(signalList, wxSizerFlags(0).Expand().Border(wxALL, 2));

	theCanvas = new OscopeCanvas(this, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS|wxSUNKEN_BORDER);
	contentSizer->Add(theCanvas, wxSizerFlags(1).Expand());

	oSizer->Add(contentSizer, wxSizerFlags(1).Expand());
	SetSizer(oSizer);

	// Bind events
	Bind(wxEVT_TOOL, &OscopeFrame::OnPauseToggle, this, ID_OSCOPE_PAUSE);
	Bind(wxEVT_TOOL, &OscopeFrame::OnAddSignal, this, ID_OSCOPE_ADD);
	Bind(wxEVT_TOOL, &OscopeFrame::OnRemoveSignal, this, ID_OSCOPE_REMOVE);
	Bind(wxEVT_TOOL, &OscopeFrame::OnExport, this, ID_OSCOPE_EXPORT);
	Bind(wxEVT_TOOL, &OscopeFrame::OnLoad, this, ID_OSCOPE_LOAD);
	Bind(wxEVT_TOOL, &OscopeFrame::OnSave, this, ID_OSCOPE_SAVE);
}

void OscopeFrame::UpdateData(void){
	if (!paused) {
		theCanvas->UpdateData();
	}
}

void OscopeFrame::UpdateMenu(void){
	theCanvas->UpdateMenu();
}

void OscopeFrame::OnPauseToggle( wxCommandEvent& event ){
	paused = oscopeToolBar->GetToolState(ID_OSCOPE_PAUSE);
	if (!paused) {
		theCanvas->clearData();
	}
}

void OscopeFrame::OnAddSignal( wxCommandEvent& event ){
	if (availableFeeds.empty()) return;

	wxMenu menu;
	for (unsigned int i = 0; i < availableFeeds.size(); ++i) {
		menu.Append(ID_OSCOPE_SIGNAL_MENU_BASE + i, availableFeeds[i]);
	}

	menu.Bind(wxEVT_MENU, &OscopeFrame::OnSignalMenuSelect, this);

	wxPoint pos = oscopeToolBar->GetPosition();
	PopupMenu(&menu, pos.x, pos.y + oscopeToolBar->GetSize().GetHeight());
}

void OscopeFrame::OnSignalMenuSelect( wxCommandEvent& event ){
	int idx = event.GetId() - ID_OSCOPE_SIGNAL_MENU_BASE;
	if (idx >= 0 && idx < (int)availableFeeds.size()) {
		string name = availableFeeds[idx];
		// Don't add duplicates
		for (unsigned int i = 0; i < feedNames.size(); ++i) {
			if (feedNames[i] == name) return;
		}
		appendNewFeed(name);
		theCanvas->UpdateMenu();
	}
}

void OscopeFrame::OnRemoveSignal( wxCommandEvent& event ){
	int sel = signalList->GetSelection();
	if (sel == wxNOT_FOUND) return;
	removeFeed(sel);
	theCanvas->UpdateMenu();
}

void OscopeFrame::OnExport( wxCommandEvent& event ){
	wxSize canvasSize = theCanvas->GetClientSize();
	wxImage circuitImage = theCanvas->generateImage();
	wxBitmap circuitBitmap(circuitImage);

	int labelAreaWidth = 100;
	int totalWidth = labelAreaWidth + canvasSize.GetWidth();
	int totalHeight = canvasSize.GetHeight();

	wxMemoryDC memDC;
	wxBitmap labelBitmap(totalWidth, totalHeight);
	memDC.SelectObject(labelBitmap);
	memDC.SetBackground(*wxWHITE_BRUSH);
	memDC.Clear();
	wxFont font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	memDC.SetFont(font);
	memDC.SetTextForeground(*wxBLACK);
	memDC.SetTextBackground(*wxWHITE);
	for (unsigned int i = 0; i < numberOfFeeds(); ++i) {
		memDC.DrawText(getFeedName(i), wxPoint(5, getFeedYPos(i)));
	}
	memDC.DrawBitmap(circuitBitmap, labelAreaWidth, 0, false);
	memDC.SelectObject(wxNullBitmap);

	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxBitmapDataObject(labelBitmap));
		wxTheClipboard->Close();
	}
}

void OscopeFrame::OnLoad( wxCommandEvent& event ){
	wxString caption = "Open an O-scope Layout";
	wxString wildcard = "CEDAR O-scope Layout files (*.cdo)|*.cdo";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();
		ifstream inFile(path.ToStdString());
		string lineFile;
		getline(inFile, lineFile, '\n');
		if (lineFile != "OSCOPE LAYOUT FILE") return;
		unsigned int numLines = 0;
		inFile >> numLines;
		getline(inFile, lineFile, '\n');

		// Remove old feeds
		feedNames.clear();
		signalList->Clear();

		for (unsigned int i = 0; i < numLines; i++) {
			getline(inFile, lineFile, '\n');
			if (lineFile != NONE_STR) {
				appendNewFeed(lineFile);
			}
		}
		Layout();
		theCanvas->UpdateMenu();
		theCanvas->clearData();
	}
}

void OscopeFrame::OnSave( wxCommandEvent& event ){
	wxString caption = "Save o-scope layout";
	wxString wildcard = "CEDAR O-scope Layout files (*.cdo)|*.cdo";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();
		string openedFilename = path.ToStdString();
		ofstream outFile(openedFilename);
		outFile << "OSCOPE LAYOUT FILE" << endl;
		outFile << numberOfFeeds() << " : following lines are order of inputs" << endl;
		for (unsigned int i = 0; i < numberOfFeeds(); i++) outFile << getFeedName(i) << endl;
		outFile.close();
	}
}

void OscopeFrame::appendNewFeed( string newName ){
	if (newName == NONE_STR) return;
	feedNames.push_back(newName);
	signalList->Append(newName);
}

void OscopeFrame::setFeedName( int i, string newName ){
	if (i < 0 || i >= (int)feedNames.size()) return;
	feedNames[i] = newName;
	signalList->SetString(i, newName);
}

unsigned int OscopeFrame::numberOfFeeds(){
	return feedNames.size();
}

void OscopeFrame::removeFeed( int i ){
	if (i < 0 || i >= (int)feedNames.size()) return;
	feedNames.erase(feedNames.begin() + i);
	signalList->Delete(i);
}

string OscopeFrame::getFeedName( int i ){
	if (i < 0 || i >= (int)feedNames.size()) return NONE_STR;
	return feedNames[i];
}

void OscopeFrame::cancelFeed( int i ){
	removeFeed(i);
}

int OscopeFrame::getFeedYPos( int i ){
	if (numberOfFeeds() == 0) return 0;
	// Match the GL coordinate mapping from OscopeCanvas::OnRender:
	//   gluOrtho2D(0, OSCOPE_HORIZONTAL, numberOfWires * 1.5, -0.25)
	// Wire i occupies GL y range [i*1.5, i*1.5+1], center at i*1.5+0.5
	// GL top = -0.25, GL bottom = n * 1.5
	// pixelY = (glY + 0.25) / (n * 1.5 + 0.25) * canvasHeight
	wxSize canvasSize = theCanvas->GetClientSize();
	int canvasHeight = canvasSize.GetHeight();
	if (canvasHeight <= 0) canvasHeight = 200;

	unsigned int n = numberOfFeeds();
	double glY = i * 1.5 + 0.5;
	return (int)((glY + 0.25) / (n * 1.5 + 0.25) * canvasHeight);
}

void OscopeFrame::updatePossableFeeds( vector< string >* newPossabilities ){
	availableFeeds = *newPossabilities;

	// Remove any active feeds that are no longer valid
	for (int i = (int)feedNames.size() - 1; i >= 0; --i) {
		bool found = false;
		for (unsigned int j = 0; j < availableFeeds.size(); ++j) {
			if (availableFeeds[j] == feedNames[i]) {
				found = true;
				break;
			}
		}
		if (!found) {
			removeFeed(i);
		}
	}
}
