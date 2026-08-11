#include "SettingsDialog.h"
#include "Settings.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

DECLARE_APP(MainApp)

// The selectable auto-router grid sizes (world units), KiCad-style. Index 0 is
// "off" (libavoid's exact coordinates). Kept in sync with the labels below.
static const double kRoutingGridValues[] = { 0.0, 1.0, 0.5, 0.25, 0.1 };
static const char*  kRoutingGridLabels[] = { "Off (exact)", "1.0", "0.5", "0.25", "0.1" };
static const int    kRoutingGridCount    = 5;

SettingsDialog::SettingsDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Preferences", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE) {

	auto& settings = appConfig().appSettings;

	wxFlexGridSizer* grid = new wxFlexGridSizer(5, 2, 8, 12);
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

	grid->Add(new wxStaticText(this, wxID_ANY, "Refresh Rate (FPS)"), 0, wxALIGN_CENTER_VERTICAL);
	int currentFps = (settings.refreshRate > 0) ? 1000 / settings.refreshRate : 60;
	refreshRateCtrl = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 1000, currentFps);
	grid->Add(refreshRateCtrl, 0, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Auto-router Grid"), 0, wxALIGN_CENTER_VERTICAL);
	routingGridCtrl = new wxChoice(this, wxID_ANY);
	int gridSel = 2; // default to 0.5
	for (int i = 0; i < kRoutingGridCount; i++) {
		routingGridCtrl->Append(kRoutingGridLabels[i]);
		if (settings.routingGridSize == (float)kRoutingGridValues[i]) gridSel = i;
	}
	routingGridCtrl->SetSelection(gridSel);
	grid->Add(routingGridCtrl, 0, wxEXPAND);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
	topSizer->Add(grid, 1, wxALL | wxEXPAND, 16);
	topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 8);

	SetSizerAndFit(topSizer);
}

bool SettingsDialog::getWireConnVisible() const { return wireConnVisibleCtrl->GetValue(); }
double SettingsDialog::getWireConnRadius() const { return wireConnRadiusCtrl->GetValue(); }
bool SettingsDialog::getGridlineVisible() const { return gridlineVisibleCtrl->GetValue(); }
int SettingsDialog::getRefreshRate() const {
	int fps = refreshRateCtrl->GetValue();
	return (fps > 0) ? 1000 / fps : 16;
}
double SettingsDialog::getRoutingGridSize() const {
	int sel = routingGridCtrl->GetSelection();
	if (sel < 0 || sel >= kRoutingGridCount) return 0.5;
	return kRoutingGridValues[sel];
}
