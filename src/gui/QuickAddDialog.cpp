#include "QuickAddDialog.h"
#include "GateLibrary.h"
#include "wx/listbox.h"
#include "wx/textctrl.h"
#include "wx/statbmp.h"
#include "wx/dcmemory.h"
#include "wx/sizer.h"
#include "wx/graphics.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cfloat>

DECLARE_APP(MainApp)

#define ID_SEARCH_FIELD 7770
#define ID_RESULT_LIST 7771
#define PREVIEW_SIZE 128

// A white-cleared bitmap. Two gotchas rolled into one helper: plain
// `wxBitmap(w,h)` is uninitialized (garbage, often black), and the default depth
// gives it a 32-bit alpha channel that GDI drawing never fills -- so the alpha
// stays 0 (fully transparent) and the image blanks out the moment the static
// control repaints through its alpha path. Force 24-bit (no alpha) and clear.
static wxBitmap blankPreview(int width, int height) {
	wxBitmap bmp(width, height, 24);
	wxMemoryDC dc(bmp);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();
	return bmp;
}

QuickAddDialog::QuickAddDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Add Component", wxDefaultPosition, wxSize(480, 400),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

	// Collect all gates from all libraries
	auto& libraries = gateLibrary().libraries;
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
	wxBitmap blank = blankPreview(PREVIEW_SIZE, PREVIEW_SIZE);
	previewImage = new wxGenericStaticBitmap(this, wxID_ANY, blank, wxDefaultPosition, wxSize(PREVIEW_SIZE, PREVIEW_SIZE));
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
	string libName = gateLibrary().gateNameToLibrary[gateName];
	if (libName.empty()) return blankPreview(width, height);

	LibraryGate& gateDef = gateLibrary().libraries[libName][gateName];

	// Flatten every stroke -- straight lines plus the structured arcs and circles
	// (Workstream G) -- into segments in gate space. The old preview drew only
	// gateDef.shape, so curved bodies and inversion bubbles went missing, and a
	// curve-only gate (empty shape) rendered as a garbage bitmap.
	struct Seg { float x1, y1, x2, y2; };
	vector<Seg> segs;

	for (auto& line : gateDef.shape)
		segs.push_back({ line.x1, line.y1, line.x2, line.y2 });

	// Arc/circle tessellation matches guiGate's GL path: angle in degrees from
	// +Y increasing clockwise toward +X, point = (cx + r*sin, cy + r*cos).
	const float DEG = 3.14159265358979323846f / 180.0f;

	for (auto& a : gateDef.arcs) {
		const int N = 48;
		float px = a.cx + a.r * sinf(a.startDeg * DEG);
		float py = a.cy + a.r * cosf(a.startDeg * DEG);
		for (int i = 1; i <= N; i++) {
			float d = (a.startDeg + a.sweepDeg * (float)i / (float)N) * DEG;
			float x = a.cx + a.r * sinf(d), y = a.cy + a.r * cosf(d);
			segs.push_back({ px, py, x, y });
			px = x; py = y;
		}
	}

	for (auto& c : gateDef.circles) {
		int N = c.segs > 0 ? c.segs : 12;
		float px = c.cx, py = c.cy + c.r;  // start at the top, as the GL path does
		for (int i = 1; i <= N; i++) {
			float d = (360.0f * (float)i / (float)N) * DEG;
			float x = c.cx + c.r * sinf(d), y = c.cy + c.r * cosf(d);
			segs.push_back({ px, py, x, y });
			px = x; py = y;
		}
	}

	if (segs.empty()) return blankPreview(width, height);

	// Frame by the true extent of every stroke, not just the lines.
	float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
	for (auto& s : segs) {
		minX = min({minX, s.x1, s.x2});
		minY = min({minY, s.y1, s.y2});
		maxX = max({maxX, s.x1, s.x2});
		maxY = max({maxY, s.y1, s.y2});
	}

	float shapeW = maxX - minX;
	float shapeH = maxY - minY;
	if (shapeW < 0.001f) shapeW = 1.0f;
	if (shapeH < 0.001f) shapeH = 1.0f;

	// Supersampled anti-aliasing: plain GDI lines are crisp but jagged, while a
	// straight anti-aliased stroke at this size looks soft/blurry. So render at
	// SS times the resolution with a wxGraphicsContext (GDI+/Direct2D smooths the
	// edges), then downscale with a high-quality filter -- crisp AND smooth.
	const int SS = 3;
	const int W = width * SS, H = height * SS;
	const int margin = 12 * SS;
	const int drawW = W - 2 * margin;
	const int drawH = H - 2 * margin;

	float scale = min((float)drawW / shapeW, (float)drawH / shapeH);
	float offsetX = margin + (drawW - shapeW * scale) / 2.0f;
	float offsetY = margin + (drawH - shapeH * scale) / 2.0f;

	wxBitmap big(W, H, 24);  // 24-bit: no alpha channel (see blankPreview)
	wxMemoryDC dc(big);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();

	wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
	if (gc) {
		gc->SetPen(wxPen(*wxBLACK, 2.0 * SS));  // ~2px once downscaled
		wxGraphicsPath path = gc->CreatePath();
		for (auto& s : segs) {
			path.MoveToPoint(offsetX + (s.x1 - minX) * scale, offsetY + (maxY - s.y1) * scale);
			path.AddLineToPoint(offsetX + (s.x2 - minX) * scale, offsetY + (maxY - s.y2) * scale);
		}
		gc->StrokePath(path);
		delete gc;  // flush the drawing into the bitmap before it's read back
	} else {
		dc.SetPen(wxPen(*wxBLACK, 2 * SS));
		for (auto& s : segs) {
			dc.DrawLine((int)(offsetX + (s.x1 - minX) * scale), (int)(offsetY + (maxY - s.y1) * scale),
			            (int)(offsetX + (s.x2 - minX) * scale), (int)(offsetY + (maxY - s.y2) * scale));
		}
	}

	wxImage img = big.ConvertToImage();
	img.Rescale(width, height, wxIMAGE_QUALITY_HIGH);
	return wxBitmap(img);
}

void QuickAddDialog::updatePreview() {
	int sel = resultList->GetSelection();
	if (sel == wxNOT_FOUND) {
		previewImage->SetBitmap(blankPreview(PREVIEW_SIZE, PREVIEW_SIZE));
		return;
	}

	wxStringClientData* data = (wxStringClientData*)resultList->GetClientObject(sel);
	if (!data) return;
	string gateName = data->GetData().ToStdString();

	auto it = previewCache.find(gateName);
	if (it == previewCache.end()) {
		it = previewCache.emplace(gateName, renderGatePreview(gateName, PREVIEW_SIZE, PREVIEW_SIZE)).first;
	}
	previewImage->SetBitmap(it->second);
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
