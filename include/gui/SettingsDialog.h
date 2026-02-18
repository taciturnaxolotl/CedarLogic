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
	bool getGridlineVisible() const;
	int getRefreshRate() const;

private:
	wxCheckBox* wireConnVisibleCtrl;
	wxSpinCtrlDouble* wireConnRadiusCtrl;
	wxCheckBox* gridlineVisibleCtrl;
	wxSpinCtrl* refreshRateCtrl;
};

#endif
