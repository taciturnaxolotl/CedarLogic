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
	std::string helpFile;
	// The values MainApp::loadSettings falls back to when the config has nothing
	// to say. They live here as well so a build with no config at all -- the
	// browser, a test, a headless render -- starts from the same place instead
	// of from whatever was on the stack.
	std::string lastDir;
	int mainFrameWidth = 1024;
	int mainFrameHeight = 768;
	int mainFrameLeft = -1;
	int mainFrameTop = -1;
	int timePerStep = 25;        // ms of circuit time per simulation step
	int refreshRate = 16;        // ms between repaints (~60 FPS)
	int autosaveSeconds = 180;   // 0 disables autosave entirely
	float wireConnRadius = 0.18f;
	bool wireConnVisible = true;
	bool gridlineVisible = true;
	bool rightClickRotate = true;
};

class Settings {
public:
	ApplicationSettings appSettings;
	// Milliseconds of simulated time per step (derived from appSettings.timePerStep).
	unsigned long timeStepMod = 25;
	// Directory the on-disk resources load from (may differ from cwd). Only the
	// help book lives there now; see EmbeddedRes.h for the rest.
	std::string resourcesDir;
};

// The process-wide settings. Lives for the whole run; constructed on first use.
Settings& appConfig();
