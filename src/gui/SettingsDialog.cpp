#include "SettingsDialog.h"
#include "Settings.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

DECLARE_APP(MainApp)

SettingsDialog::SettingsDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Preferences", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE) {

	auto& settings = appConfig().appSettings;

	wxFlexGridSizer* grid = new wxFlexGridSizer(6, 2, 8, 12);
	grid->AddGrowableCol(1, 1);

	grid->Add(new wxStaticText(this, wxID_ANY, "Wire Connection Points"), 0, wxALIGN_CENTER_VERTICAL);
	wireConnVisibleCtrl = new wxCheckBox(this, wxID_ANY, "");
	wireConnVisibleCtrl->SetValue(settings.wireConnVisible);
	grid->Add(wireConnVisibleCtrl, 0, wxALIGN_CENTER_VERTICAL);

	grid->Add(new wxStaticText(this, wxID_ANY, "Wire Connection Radius"), 0, wxALIGN_CENTER_VERTICAL);
	wireConnRadiusCtrl = new wxSpinCtrlDouble(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0.05, 1.0, settings.wireConnRadius, 0.01);
	grid->Add(wireConnRadiusCtrl, 0, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Display Gridlines"), 0, wxALIGN_CENTER_VERTICAL);
	gridlineVisibleCtrl = new wxCheckBox(this, wxID_ANY, "");
	gridlineVisibleCtrl->SetValue(settings.gridlineVisible);
	grid->Add(gridlineVisibleCtrl, 0, wxALIGN_CENTER_VERTICAL);

	grid->Add(new wxStaticText(this, wxID_ANY, "Right-Click Rotates Gates"), 0, wxALIGN_CENTER_VERTICAL);
	rightClickRotateCtrl = new wxCheckBox(this, wxID_ANY, "");
	rightClickRotateCtrl->SetValue(settings.rightClickRotate);
	grid->Add(rightClickRotateCtrl, 0, wxALIGN_CENTER_VERTICAL);

	grid->Add(new wxStaticText(this, wxID_ANY, "Refresh Rate (FPS)"), 0, wxALIGN_CENTER_VERTICAL);
	int currentFps = (settings.refreshRate > 0) ? 1000 / settings.refreshRate : 60;
	refreshRateCtrl = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 1000, currentFps);
	grid->Add(refreshRateCtrl, 0, wxEXPAND);

	// Minutes, because that is how someone thinks about how much work they are
	// willing to lose. Zero turns it off, for anyone editing on a slow share who
	// would rather not have the app touch the disk on a timer.
	grid->Add(new wxStaticText(this, wxID_ANY, "Autosave Every (minutes, 0 = off)"), 0, wxALIGN_CENTER_VERTICAL);
	int currentMinutes = (settings.autosaveSeconds + 59) / 60;   // round up; 0 stays 0
	autosaveMinutesCtrl = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 60, currentMinutes);
	grid->Add(autosaveMinutesCtrl, 0, wxEXPAND);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
	topSizer->Add(grid, 1, wxALL | wxEXPAND, 16);
	topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 8);

	SetSizerAndFit(topSizer);
}

bool SettingsDialog::getWireConnVisible() const { return wireConnVisibleCtrl->GetValue(); }
double SettingsDialog::getWireConnRadius() const { return wireConnRadiusCtrl->GetValue(); }
bool SettingsDialog::getGridlineVisible() const { return gridlineVisibleCtrl->GetValue(); }
bool SettingsDialog::getRightClickRotate() const { return rightClickRotateCtrl->GetValue(); }
int SettingsDialog::getAutosaveSeconds() const {
	return autosaveMinutesCtrl->GetValue() * 60;
}

int SettingsDialog::getRefreshRate() const {
	int fps = refreshRateCtrl->GetValue();
	return (fps > 0) ? 1000 / fps : 16;
}
