#include "QuickAddDialog.h"
#include "wx/listbox.h"
#include "wx/textctrl.h"
#include "wx/statbmp.h"
#include "wx/dcmemory.h"
#include "wx/sizer.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cfloat>

DECLARE_APP(MainApp)

#define ID_SEARCH_FIELD 7770
#define ID_RESULT_LIST 7771
#define PREVIEW_SIZE 128

QuickAddDialog::QuickAddDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Add Component", wxDefaultPosition, wxSize(480, 400),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

	// Collect all gates from all libraries
	auto& libraries = wxGetApp().libraries;
	for (auto& libPair : libraries) {
		for (auto& gatePair : libPair.second) {
			GateEntry entry;
			entry.gateName = gatePair.first;
			entry.caption = gatePair.second.caption;
			entry.libraryName = libPair.first;
			allGates.push_back(entry);
		}
	}

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	searchField = new wxTextCtrl(this, ID_SEARCH_FIELD, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	topSizer->Add(searchField, 0, wxEXPAND | wxALL, 16);

	wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);

	resultList = new wxListBox(this, ID_RESULT_LIST, wxDefaultPosition, wxDefaultSize, 0, NULL, wxLB_SINGLE);
	contentSizer->Add(resultList, 1, wxEXPAND | wxRIGHT, 12);

	// Preview image on the right
	wxBitmap blank(PREVIEW_SIZE, PREVIEW_SIZE);
	{
		wxMemoryDC dc(blank);
		dc.SetBackground(*wxWHITE_BRUSH);
		dc.Clear();
	}
	previewImage = new wxStaticBitmap(this, wxID_ANY, blank, wxDefaultPosition, wxSize(PREVIEW_SIZE, PREVIEW_SIZE));
	contentSizer->Add(previewImage, 0, wxALIGN_TOP);

	topSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

	SetSizer(topSizer);

	// Populate with all gates initially
	updateList("");

	// Bind events
	searchField->Bind(wxEVT_TEXT, &QuickAddDialog::OnTextChanged, this);
	searchField->Bind(wxEVT_KEY_DOWN, &QuickAddDialog::OnTextKey, this);
	resultList->Bind(wxEVT_LISTBOX_DCLICK, &QuickAddDialog::OnListDClick, this);
	resultList->Bind(wxEVT_LISTBOX, &QuickAddDialog::OnListSelect, this);

	searchField->SetFocus();
}

wxBitmap QuickAddDialog::renderGatePreview(const string& gateName, int width, int height) {
	string libName = wxGetApp().gateNameToLibrary[gateName];
	if (libName.empty()) return wxBitmap(width, height);

	LibraryGate& gateDef = wxGetApp().libraries[libName][gateName];
	if (gateDef.shape.empty()) return wxBitmap(width, height);

	// Find bounding box of all lines
	float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
	for (auto& line : gateDef.shape) {
		minX = min({minX, line.x1, line.x2});
		minY = min({minY, line.y1, line.y2});
		maxX = max({maxX, line.x1, line.x2});
		maxY = max({maxY, line.y1, line.y2});
	}

	float shapeW = maxX - minX;
	float shapeH = maxY - minY;
	if (shapeW < 0.001f) shapeW = 1.0f;
	if (shapeH < 0.001f) shapeH = 1.0f;

	int margin = 12;
	int drawW = width - 2 * margin;
	int drawH = height - 2 * margin;

	float scale = min((float)drawW / shapeW, (float)drawH / shapeH);
	float offsetX = margin + (drawW - shapeW * scale) / 2.0f;
	float offsetY = margin + (drawH - shapeH * scale) / 2.0f;

	wxBitmap bmp(width, height);
	wxMemoryDC dc(bmp);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();
	dc.SetPen(wxPen(*wxBLACK, 2));

	for (auto& line : gateDef.shape) {
		int x1 = (int)(offsetX + (line.x1 - minX) * scale);
		int y1 = (int)(offsetY + (line.y1 - minY) * scale);
		int x2 = (int)(offsetX + (line.x2 - minX) * scale);
		int y2 = (int)(offsetY + (line.y2 - minY) * scale);
		dc.DrawLine(x1, y1, x2, y2);
	}

	return bmp;
}

void QuickAddDialog::updatePreview() {
	int sel = resultList->GetSelection();
	if (sel == wxNOT_FOUND) {
		wxBitmap blank(PREVIEW_SIZE, PREVIEW_SIZE);
		wxMemoryDC dc(blank);
		dc.SetBackground(*wxWHITE_BRUSH);
		dc.Clear();
		previewImage->SetBitmap(blank);
		return;
	}

	wxStringClientData* data = (wxStringClientData*)resultList->GetClientObject(sel);
	if (!data) return;
	string gateName = data->GetData().ToStdString();

	auto it = previewCache.find(gateName);
	if (it != previewCache.end()) {
		previewImage->SetBitmap(it->second);
	} else {
		wxBitmap bmp = renderGatePreview(gateName, PREVIEW_SIZE, PREVIEW_SIZE);
		previewCache[gateName] = bmp;
		previewImage->SetBitmap(bmp);
	}
}

int QuickAddDialog::fuzzyScore(const string& query, const string& target) {
	if (query.empty()) return 0;

	string lowerQuery, lowerTarget;
	for (char c : query) lowerQuery += tolower(c);
	for (char c : target) lowerTarget += tolower(c);

	// Exact substring match gets highest score
	if (lowerTarget.find(lowerQuery) != string::npos) {
		// Prefer matches at the start
		if (lowerTarget.find(lowerQuery) == 0) return 100;
		return 80;
	}

	// Fuzzy: all query chars must appear in order
	int qi = 0;
	int score = 0;
	int lastMatch = -1;
	for (int ti = 0; ti < (int)lowerTarget.size() && qi < (int)lowerQuery.size(); ti++) {
		if (lowerTarget[ti] == lowerQuery[qi]) {
			score += 10;
			// Bonus for consecutive matches
			if (lastMatch == ti - 1) score += 5;
			// Bonus for matching at word boundaries
			if (ti == 0 || lowerTarget[ti - 1] == ' ' || lowerTarget[ti - 1] == '-' || lowerTarget[ti - 1] == '_')
				score += 5;
			lastMatch = ti;
			qi++;
		}
	}

	// All query chars must match
	if (qi < (int)lowerQuery.size()) return -1;
	return score;
}

void QuickAddDialog::updateList(const string& query) {
	resultList->Clear();

	struct ScoredEntry {
		int score;
		string displayText;
		string gateName;
	};
	vector<ScoredEntry> scored;

	for (auto& entry : allGates) {
		// Score against both caption and gate name
		int captionScore = fuzzyScore(query, entry.caption);
		int nameScore = fuzzyScore(query, entry.gateName);
		int bestScore = max(captionScore, nameScore);

		if (query.empty() || bestScore > 0) {
			string display = entry.caption;
			if (entry.caption != entry.gateName) {
				display += "  [" + entry.gateName + "]";
			}
			scored.push_back({bestScore, display, entry.gateName});
		}
	}

	// Sort by score descending
	sort(scored.begin(), scored.end(), [](const ScoredEntry& a, const ScoredEntry& b) {
		return a.score > b.score;
	});

	for (auto& s : scored) {
		resultList->Append(s.displayText, new wxStringClientData(s.gateName));
	}

	if (resultList->GetCount() > 0) {
		resultList->SetSelection(0);
	}

	updatePreview();
}

void QuickAddDialog::OnTextChanged(wxCommandEvent& evt) {
	updateList(searchField->GetValue().ToStdString());
}

void QuickAddDialog::OnTextKey(wxKeyEvent& evt) {
	int key = evt.GetKeyCode();
	if (key == WXK_DOWN) {
		int sel = resultList->GetSelection();
		if (sel < (int)resultList->GetCount() - 1) {
			resultList->SetSelection(sel + 1);
			updatePreview();
		}
	} else if (key == WXK_UP) {
		int sel = resultList->GetSelection();
		if (sel > 0) {
			resultList->SetSelection(sel - 1);
			updatePreview();
		}
	} else if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
		confirm();
	} else if (key == WXK_ESCAPE) {
		EndModal(wxID_CANCEL);
	} else {
		evt.Skip();
	}
}

void QuickAddDialog::OnListDClick(wxCommandEvent& evt) {
	confirm();
}

void QuickAddDialog::OnListSelect(wxCommandEvent& evt) {
	updatePreview();
}

void QuickAddDialog::confirm() {
	int sel = resultList->GetSelection();
	if (sel != wxNOT_FOUND) {
		wxStringClientData* data = (wxStringClientData*)resultList->GetClientObject(sel);
		if (data) {
			selectedGate = data->GetData().ToStdString();
		}
		EndModal(wxID_OK);
	}
}
