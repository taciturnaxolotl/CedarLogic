/*****************************************************************************
   Project: CEDAR Logic Simulator

   Settings: user/application settings and derived timing.

   Extracted from the MainApp God-singleton (Workstream C). Reach it through the
   free accessor appConfig() instead of wxGetApp(). ApplicationSettings lived on
   MainApp.h; it moves here so the settings are a cohesive unit and MainApp.h
   pulls it back in via this header.
*****************************************************************************/

#pragma once

#include <string>

struct ApplicationSettings {
	std::string gateLibFile;
	std::string textFontFile;
	std::string helpFile;
	std::string lastDir;
	int mainFrameWidth;
	int mainFrameHeight;
	int mainFrameLeft;
	int mainFrameTop;
	int timePerStep;
	int refreshRate;
	float wireConnRadius;
	bool wireConnVisible;
	bool gridlineVisible;
	bool rightClickRotate;
};

class Settings {
public:
	ApplicationSettings appSettings;
	// Milliseconds of simulated time per step (derived from appSettings.timePerStep).
	unsigned long timeStepMod = 0;
	// Directory the executable's resources load from (may differ from cwd).
	std::string resourcesDir;
};

// The process-wide settings. Lives for the whole run; constructed on first use.
Settings& appConfig();
