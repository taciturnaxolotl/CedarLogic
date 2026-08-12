#ifndef QUICKADDDIALOG_H_
#define QUICKADDDIALOG_H_

#include "MainApp.h"
#include "LibraryParse.h"
#include <vector>
#include <string>
#include <map>
#include <wx/bitmap.h>
#include <wx/generic/statbmpg.h>

using namespace std;

class QuickAddDialog : public wxDialog {
public:
	QuickAddDialog(wxWindow* parent);

	string getSelectedGate() const { return selectedGate; }

private:
	void OnTextChanged(wxCommandEvent& evt);
	void OnTextKey(wxKeyEvent& evt);
	void OnListDClick(wxCommandEvent& evt);
	void OnListSelect(wxCommandEvent& evt);
	void updateList(const string& query);
	void updatePreview();
	void confirm();
	int fuzzyScore(const string& query, const string& target);
	wxBitmap renderGatePreview(const string& gateName, int width, int height);

	wxTextCtrl* searchField;
	wxListBox* resultList;
	// Owner-drawn (generic) static bitmap, NOT the native wxStaticBitmap: on MSW
	// the native STATIC control won't re-blit a bitmap handle it previously held
	// and swapped away from, so a cached preview shown once never repainted when
	// you navigated back to it. The generic control paints via wxDC every time.
	wxGenericStaticBitmap* previewImage;
	string selectedGate;

	struct GateEntry {
		string gateName;
		string caption;
		string libraryName;
	};
	vector<GateEntry> allGates;
	map<string, wxBitmap> previewCache;
};

#endif
