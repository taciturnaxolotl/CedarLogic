#ifndef SETTINGSDIALOG_H_
#define SETTINGSDIALOG_H_

#include "MainApp.h"
#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

class SettingsDialog : public wxDialog {
public:
	SettingsDialog(wxWindow* parent);

	bool getWireConnVisible() const;
	double getWireConnRadius() const;
	int getPinHitZone() const;
	int getWireHitZone() const;
	bool getGridlineVisible() const;
	bool getRightClickRotate() const;
	int getRefreshRate() const;
	int getAutosaveSeconds() const;   // 0 = off

private:
	wxCheckBox* wireConnVisibleCtrl;
	wxSpinCtrlDouble* wireConnRadiusCtrl;
	wxSpinCtrl* pinHitZoneCtrl;
	wxSpinCtrl* wireHitZoneCtrl;
	wxCheckBox* gridlineVisibleCtrl;
	wxCheckBox* rightClickRotateCtrl;
	wxSpinCtrl* refreshRateCtrl;
	wxSpinCtrl* autosaveMinutesCtrl;
};

#endif
